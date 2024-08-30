#include "worker.hh"
#include "substitution-goal.hh"
#include "nar-info.hh"
#include "signals.hh"
#include "finally.hh"

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
    state = &PathSubstitutionGoal::init;
    name = fmt("substitution of '%s'", worker.store.printStorePath(this->storePath));
    trace("created");
    maintainExpectedSubstitutions = std::make_unique<MaintainCount<uint64_t>>(worker.expectedSubstitutions);
}


PathSubstitutionGoal::~PathSubstitutionGoal()
{
    cleanup();
}


Goal::Finished PathSubstitutionGoal::done(
    ExitCode result,
    BuildResult::Status status,
    std::optional<std::string> errorMsg)
{
    buildResult.status = status;
    if (errorMsg) {
        debug(*errorMsg);
        buildResult.errorMsg = *errorMsg;
    }
    return Finished{result};
}


Goal::WorkResult PathSubstitutionGoal::work(bool inBuildSlot)
{
    return (this->*state)(inBuildSlot);
}


Goal::WorkResult PathSubstitutionGoal::init(bool inBuildSlot)
{
    trace("init");

    worker.store.addTempRoot(storePath);

    /* If the path already exists we're done. */
    if (!repair && worker.store.isValidPath(storePath)) {
        return done(ecSuccess, BuildResult::AlreadyValid);
    }

    if (settings.readOnlyMode)
        throw Error("cannot substitute path '%s' - no write access to the Nix store", worker.store.printStorePath(storePath));

    subs = settings.useSubstitutes ? getDefaultSubstituters() : std::list<ref<Store>>();

    return tryNext(inBuildSlot);
}


Goal::WorkResult PathSubstitutionGoal::tryNext(bool inBuildSlot)
{
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
        return done(
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
        if (sub->storeDir == worker.store.storeDir)
            assert(subPath == storePath);
    } else if (sub->storeDir != worker.store.storeDir) {
        return tryNext(inBuildSlot);
    }

    try {
        // FIXME: make async
        info = sub->queryPathInfo(subPath ? *subPath : storePath);
    } catch (InvalidPath &) {
        return tryNext(inBuildSlot);
    } catch (SubstituterDisabled &) {
        if (settings.tryFallback) {
            return tryNext(inBuildSlot);
        }
        throw;
    } catch (Error & e) {
        if (settings.tryFallback) {
            logError(e.info());
            return tryNext(inBuildSlot);
        }
        throw;
    }

    if (info->path != storePath) {
        if (info->isContentAddressed(*sub) && info->references.empty()) {
            auto info2 = std::make_shared<ValidPathInfo>(*info);
            info2->path = storePath;
            info = info2;
        } else {
            printError("asked '%s' for '%s' but got '%s'",
                sub->getUri(), worker.store.printStorePath(storePath), sub->printStorePath(info->path));
            return tryNext(inBuildSlot);
        }
    }

    /* Update the total expected download size. */
    auto narInfo = std::dynamic_pointer_cast<const NarInfo>(info);

    maintainExpectedNar = std::make_unique<MaintainCount<uint64_t>>(worker.expectedNarSize, info->narSize);

    maintainExpectedDownload =
        narInfo && narInfo->fileSize
        ? std::make_unique<MaintainCount<uint64_t>>(worker.expectedDownloadSize, narInfo->fileSize)
        : nullptr;

    /* Bail out early if this substituter lacks a valid
       signature. LocalStore::addToStore() also checks for this, but
       only after we've downloaded the path. */
    if (!sub->isTrusted && worker.store.pathInfoIsUntrusted(*info))
    {
        warn("ignoring substitute for '%s' from '%s', as it's not signed by any of the keys in 'trusted-public-keys'",
            worker.store.printStorePath(storePath), sub->getUri());
        return tryNext(inBuildSlot);
    }

    /* To maintain the closure invariant, we first have to realise the
       paths referenced by this one. */
    WaitForGoals result;
    for (auto & i : info->references)
        if (i != storePath) /* ignore self-references */
            result.goals.insert(worker.goalFactory().makePathSubstitutionGoal(i));

    if (result.goals.empty()) {/* to prevent hang (no wake-up event) */
        return referencesValid(inBuildSlot);
    } else {
        state = &PathSubstitutionGoal::referencesValid;
        return result;
    }
}


Goal::WorkResult PathSubstitutionGoal::referencesValid(bool inBuildSlot)
{
    trace("all references realised");

    if (nrFailed > 0) {
        return done(
            nrNoSubstituters > 0 || nrIncompleteClosure > 0 ? ecIncompleteClosure : ecFailed,
            BuildResult::DependencyFailed,
            fmt("some references of path '%s' could not be realised", worker.store.printStorePath(storePath)));
    }

    for (auto & i : info->references)
        if (i != storePath) /* ignore self-references */
            assert(worker.store.isValidPath(i));

    state = &PathSubstitutionGoal::tryToRun;
    return tryToRun(inBuildSlot);
}


Goal::WorkResult PathSubstitutionGoal::tryToRun(bool inBuildSlot)
{
    trace("trying to run");

    if (!inBuildSlot) {
        return WaitForSlot{};
    }

    maintainRunningSubstitutions = std::make_unique<MaintainCount<uint64_t>>(worker.runningSubstitutions);

    outPipe.create();

    thr = std::async(std::launch::async, [this]() {
        auto & fetchPath = subPath ? *subPath : storePath;
        try {
            ReceiveInterrupts receiveInterrupts;

            /* Wake up the worker loop when we're done. */
            Finally updateStats([this]() { outPipe.writeSide.close(); });

            Activity act(*logger, actSubstitute, Logger::Fields{worker.store.printStorePath(storePath), sub->getUri()});
            PushActivity pact(act.id);

            copyStorePath(
                *sub, worker.store, fetchPath, repair, sub->isTrusted ? NoCheckSigs : CheckSigs
            );
        } catch (const EndOfFile &) {
            throw EndOfFile(
                "NAR for '%s' fetched from '%s' is incomplete",
                sub->printStorePath(fetchPath),
                sub->getUri()
            );
        }
    });

    state = &PathSubstitutionGoal::finished;
    return WaitForWorld{{outPipe.readSide.get()}, true};
}


Goal::WorkResult PathSubstitutionGoal::finished(bool inBuildSlot)
{
    trace("substitute finished");

    worker.childTerminated(this);

    try {
        thr.get();
    } catch (std::exception & e) {
        printError(e.what());

        /* Cause the parent build to fail unless --fallback is given,
           or the substitute has disappeared. The latter case behaves
           the same as the substitute never having existed in the
           first place. */
        try {
            throw;
        } catch (SubstituteGone &) {
        } catch (...) {
            substituterFailed = true;
        }

        /* Try the next substitute. */
        state = &PathSubstitutionGoal::tryNext;
        return tryNext(inBuildSlot);
    }

    worker.markContentsGood(storePath);

    printMsg(lvlChatty, "substitution of path '%s' succeeded", worker.store.printStorePath(storePath));

    maintainRunningSubstitutions.reset();

    maintainExpectedSubstitutions.reset();
    worker.doneSubstitutions++;

    if (maintainExpectedDownload) {
        auto fileSize = maintainExpectedDownload->delta;
        maintainExpectedDownload.reset();
        worker.doneDownloadSize += fileSize;
    }

    worker.doneNarSize += maintainExpectedNar->delta;
    maintainExpectedNar.reset();

    return done(ecSuccess, BuildResult::Substituted);
}


Goal::WorkResult PathSubstitutionGoal::handleChildOutput(int fd, std::string_view data)
{
    return StillAlive{};
}


void PathSubstitutionGoal::cleanup()
{
    try {
        if (thr.valid()) {
            // FIXME: signal worker thread to quit.
            thr.get();
            worker.childTerminated(this);
        }

        outPipe.close();
    } catch (...) {
        ignoreException();
    }
}


}
