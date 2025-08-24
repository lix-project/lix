#include "lix/libstore/build/worker.hh"
#include "lix/libstore/build/substitution-goal.hh"
#include "lix/libstore/nar-info.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/signals.hh"
#include "lix/libutil/finally.hh"
#include <boost/outcome/try.hpp>
#include <kj/array.h>
#include <kj/vector.h>

namespace nix {

PathSubstitutionGoal::PathSubstitutionGoal(
    const StorePath & storePath,
    Worker & worker,
    bool isDependency,
    RepairFlag repair,
    std::optional<ContentAddress> ca
)
    : Goal(worker, isDependency)
    , storePath(storePath)
    , repair(repair)
    , ca(ca)
{
    name = fmt("substitution of '%s'", worker.store.printStorePath(this->storePath));
    trace("created");
    maintainExpectedSubstitutions = worker.expectedSubstitutions.addTemporarily(1);
}


PathSubstitutionGoal::~PathSubstitutionGoal()
{
    cleanup();
}


Goal::WorkResult PathSubstitutionGoal::done(
    ExitCode result,
    BuildResult::Status status,
    std::optional<std::string> errorMsg)
{
    BuildResult buildResult{.status = status};
    if (errorMsg) {
        debug("%1%", Uncolored(*errorMsg));
        buildResult.errorMsg = *errorMsg;
    }
    return WorkResult{result, std::move(buildResult)};
}


kj::Promise<Result<Goal::WorkResult>> PathSubstitutionGoal::workImpl() noexcept
try {
    trace("init");

    TRY_AWAIT(worker.store.addTempRoot(storePath));

    /* If the path already exists we're done. */
    if (!repair && TRY_AWAIT(worker.store.isValidPath(storePath))) {
        co_return done(ecSuccess, BuildResult::AlreadyValid);
    }

    if (settings.readOnlyMode)
        throw Error("cannot substitute path '%s' - no write access to the Nix store", worker.store.printStorePath(storePath));

    subs = settings.useSubstitutes ? TRY_AWAIT(getDefaultSubstituters()) : std::list<ref<Store>>();

    BOOST_OUTCOME_CO_TRY(auto result, co_await tryNext());
    result.storePath = storePath;
    co_return result;
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<Goal::WorkResult>> PathSubstitutionGoal::tryNext() noexcept
try {
    trace("trying next substituter");

    cleanup();

    if (subs.size() == 0) {
        /* None left.  Terminate this goal and let someone else deal
           with it. */
        if (substituterFailed) {
            worker.failedSubstitutions++;
        }

        /* Hack: don't indicate failure if there were no substituters.
           In that case the calling derivation should just do a
           build. */
        co_return done(
            substituterFailed ? ecFailed : ecNoSubstituters,
            BuildResult::NoSubstituters,
            fmt("path '%s' is required, but there is no substituter that can build it", worker.store.printStorePath(storePath)));
    }

    sub = subs.front();
    subs.pop_front();

    if (ca) {
        subPath = sub->makeFixedOutputPathFromCA(
            std::string { storePath.name() },
            ContentAddressWithReferences::withoutRefs(*ca));
        if (sub->config().storeDir == worker.store.config().storeDir)
            assert(subPath == storePath);
    } else if (sub->config().storeDir != worker.store.config().storeDir) {
        co_return co_await tryNext();
    }

    do {
        try {
            info = TRY_AWAIT(sub->queryPathInfo(subPath ? *subPath : storePath));
            break;
        } catch (InvalidPath &) {
        } catch (SubstituterDisabled &) {
            if (!settings.tryFallback) {
                throw;
            }
        } catch (Error & e) {
            if (settings.tryFallback) {
                logError(e.info());
            } else {
                throw;
            }
        }
        co_return co_await tryNext();
    } while (false);

    if (info->path != storePath) {
        if (info->isContentAddressed(*sub) && info->references.empty()) {
            auto info2 = std::make_shared<ValidPathInfo>(*info);
            info2->path = storePath;
            info = info2;
        } else {
            printError("asked '%s' for '%s' but got '%s'",
                sub->getUri(), worker.store.printStorePath(storePath), sub->printStorePath(info->path));
            co_return co_await tryNext();
        }
    }

    /* Update the total expected download size. */
    auto narInfo = std::dynamic_pointer_cast<const NarInfo>(info);

    maintainExpectedNar = worker.expectedNarSize.addTemporarily(info->narSize);

    maintainExpectedDownload =
        narInfo && narInfo->fileSize
        ? worker.expectedDownloadSize.addTemporarily(narInfo->fileSize)
        : nullptr;

    /* Bail out early if this substituter lacks a valid
       signature. LocalStore::addToStore() also checks for this, but
       only after we've downloaded the path. */
    if (!sub->config().isTrusted && worker.store.pathInfoIsUntrusted(*info))
    {
        printTaggedWarning(
            "ignoring substitute for '%s' from '%s', as it's not signed by any of the keys in "
            "'trusted-public-keys'",
            worker.store.printStorePath(storePath),
            sub->getUri()
        );
        co_return co_await tryNext();
    }

    /* To maintain the closure invariant, we first have to realise the
       paths referenced by this one. */
    kj::Vector<std::pair<GoalPtr, kj::Promise<Result<WorkResult>>>> dependencies;
    for (auto & i : info->references)
        if (i != storePath) /* ignore self-references */
            dependencies.add(worker.goalFactory().makePathSubstitutionGoal(i));

    if (!dependencies.empty()) {/* to prevent hang (no wake-up event) */
        TRY_AWAIT(waitForGoals(dependencies.releaseAsArray()));
    }
    co_return co_await referencesValid();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<Goal::WorkResult>> PathSubstitutionGoal::referencesValid() noexcept
try {
    trace("all references realised");

    if (nrFailed > 0) {
        co_return done(
            nrNoSubstituters > 0 || nrIncompleteClosure > 0 ? ecIncompleteClosure : ecFailed,
            BuildResult::DependencyFailed,
            fmt("some references of path '%s' could not be realised",
                worker.store.printStorePath(storePath))
        );
    }

    for (auto & i : info->references)
        if (i != storePath) /* ignore self-references */
            assert(TRY_AWAIT(worker.store.isValidPath(i)));

    co_return TRY_AWAIT(tryToRun());
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<Goal::WorkResult>> PathSubstitutionGoal::tryToRun() noexcept
try {
    trace("trying to run");

    if (!slotToken.valid()) {
        slotToken = co_await worker.substitutions.acquire();
    }

    maintainRunningSubstitutions = worker.runningSubstitutions.addTemporarily(1);

    auto pipe = kj::newPromiseAndCrossThreadFulfiller<void>();
    outPipe = kj::mv(pipe.fulfiller);

    thr = std::async(std::launch::async, [this]() {
        AsyncIoRoot aio;
        /* Wake up the worker loop when we're done. */
        Finally updateStats([this]() { outPipe->fulfill(); });

        auto & fetchPath = subPath ? *subPath : storePath;
        try {
            ReceiveInterrupts receiveInterrupts;

            Activity act(
                *logger,
                actSubstitute,
                Logger::Fields{worker.store.printStorePath(storePath), sub->getUri()}
            );

            aio.blockOn(copyStorePath(
                *sub,
                worker.store,
                fetchPath,
                repair,
                sub->config().isTrusted ? NoCheckSigs : CheckSigs,
                &act
            ));
        } catch (const EndOfFile &) {
            throw EndOfFile(
                "NAR for '%s' fetched from '%s' is incomplete",
                sub->printStorePath(fetchPath),
                sub->getUri()
            );
        }
    });

    co_await pipe.promise;
    co_return co_await finished();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<Goal::WorkResult>> PathSubstitutionGoal::finished() noexcept
try {
    trace("substitute finished");

    do {
        try {
            slotToken = {};
            thr.get();
            break;
        } catch (std::exception & e) { // NOLINT(lix-foreign-exceptions)
            printError("%1%", Uncolored(e.what()));

            /* Cause the parent build to fail unless --fallback is given,
               or the substitute has disappeared. The latter case behaves
               the same as the substitute never having existed in the
               first place. */
            if (dynamic_cast<SubstituteGone *>(&e) == nullptr) {
                substituterFailed = true;
            }
        }
        /* Try the next substitute. */
        co_return co_await tryNext();
    } while (false);

    worker.markContentsGood(storePath);

    printMsg(lvlChatty, "substitution of path '%s' succeeded", worker.store.printStorePath(storePath));

    maintainRunningSubstitutions.reset();

    maintainExpectedSubstitutions.reset();
    worker.doneSubstitutions++;

    worker.doneDownloadSize += maintainExpectedDownload.delta();
    maintainExpectedDownload.reset();

    worker.doneNarSize += maintainExpectedNar.delta();
    maintainExpectedNar.reset();

    co_return done(ecSuccess, BuildResult::Substituted);
} catch (...) {
    co_return result::current_exception();
}


void PathSubstitutionGoal::cleanup()
{
    try {
        if (thr.valid()) {
            // FIXME: signal worker thread to quit.
            thr.get();
        }
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}


}
