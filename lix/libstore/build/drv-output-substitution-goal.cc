#include "lix/libstore/build/drv-output-substitution-goal.hh"
#include "lix/libstore/build-result.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/finally.hh"
#include "lix/libstore/build/worker.hh"
#include "lix/libstore/build/substitution-goal.hh"
#include "lix/libutil/signals.hh"
#include <kj/array.h>
#include <kj/async.h>
#include <kj/vector.h>

namespace nix {

DrvOutputSubstitutionGoal::DrvOutputSubstitutionGoal(
    const DrvOutput & id,
    Worker & worker,
    bool isDependency,
    RepairFlag repair,
    std::optional<ContentAddress> ca)
    : Goal(worker, isDependency)
    , id(id)
{
    name = fmt("substitution of '%s'", id.to_string());
    trace("created");
}


kj::Promise<Result<Goal::WorkResult>> DrvOutputSubstitutionGoal::workImpl() noexcept
try {
    trace("init");

    /* If the derivation already exists, we’re done */
    if (worker.store.queryRealisation(id)) {
        co_return WorkResult{ecSuccess};
    }

    subs = settings.useSubstitutes ? getDefaultSubstituters() : std::list<ref<Store>>();
    co_return co_await tryNext();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<Goal::WorkResult>> DrvOutputSubstitutionGoal::tryNext() noexcept
try {
    trace("trying next substituter");

    if (!slotToken.valid()) {
        slotToken = co_await worker.substitutions.acquire();
    }

    maintainRunningSubstitutions = worker.runningSubstitutions.addTemporarily(1);

    if (subs.size() == 0) {
        /* None left.  Terminate this goal and let someone else deal
           with it. */
        debug("derivation output '%s' is required, but there is no substituter that can provide it", id.to_string());

        if (substituterFailed) {
            worker.failedSubstitutions++;
        }

        /* Hack: don't indicate failure if there were no substituters.
           In that case the calling derivation should just do a
           build. */
        co_return WorkResult{substituterFailed ? ecFailed : ecNoSubstituters};
    }

    sub = subs.front();
    subs.pop_front();

    /* The async call to a curl download below can outlive `this` (if
       some other error occurs), so it must not touch `this`. So put
       the shared state in a separate refcounted object. */
    downloadState = std::make_shared<DownloadState>();
    auto pipe = kj::newPromiseAndCrossThreadFulfiller<void>();
    downloadState->outPipe = kj::mv(pipe.fulfiller);

    downloadState->result =
        std::async(std::launch::async, [downloadState{downloadState}, id{id}, sub{sub}] {
            Finally updateStats([&]() { downloadState->outPipe->fulfill(); });
            ReceiveInterrupts receiveInterrupts;
            return sub->queryRealisation(id);
        });

    co_await pipe.promise;
    co_return co_await realisationFetched();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<Goal::WorkResult>> DrvOutputSubstitutionGoal::realisationFetched() noexcept
try {
    maintainRunningSubstitutions.reset();
    slotToken = {};

    try {
        outputInfo = downloadState->result.get();
    } catch (std::exception & e) {
        printError(e.what());
        substituterFailed = true;
    }

    if (!outputInfo) {
        co_return co_await tryNext();
    }

    kj::Vector<std::pair<GoalPtr, kj::Promise<Result<WorkResult>>>> dependencies;
    for (const auto & [depId, depPath] : outputInfo->dependentRealisations) {
        if (depId != id) {
            if (auto localOutputInfo = worker.store.queryRealisation(depId);
                localOutputInfo && localOutputInfo->outPath != depPath) {
                warn(
                    "substituter '%s' has an incompatible realisation for '%s', ignoring.\n"
                    "Local:  %s\n"
                    "Remote: %s",
                    sub->getUri(),
                    depId.to_string(),
                    worker.store.printStorePath(localOutputInfo->outPath),
                    worker.store.printStorePath(depPath)
                );
                co_return co_await tryNext();
            }
            dependencies.add(worker.goalFactory().makeDrvOutputSubstitutionGoal(depId));
        }
    }

    dependencies.add(worker.goalFactory().makePathSubstitutionGoal(outputInfo->outPath));

    if (!dependencies.empty()) {
        TRY_AWAIT(waitForGoals(dependencies.releaseAsArray()));
    }
    co_return co_await outPathValid();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<Goal::WorkResult>> DrvOutputSubstitutionGoal::outPathValid() noexcept
try {
    assert(outputInfo);
    trace("output path substituted");

    if (nrFailed > 0) {
        debug("The output path of the derivation output '%s' could not be substituted", id.to_string());
        return {WorkResult{
            nrNoSubstituters > 0 || nrIncompleteClosure > 0 ? ecIncompleteClosure : ecFailed,
        }};
    }

    worker.store.registerDrvOutput(*outputInfo);
    return finished();
} catch (...) {
    return {result::current_exception()};
}

kj::Promise<Result<Goal::WorkResult>> DrvOutputSubstitutionGoal::finished() noexcept
try {
    trace("finished");
    return {WorkResult{ecSuccess}};
} catch (...) {
    return {result::current_exception()};
}

}
