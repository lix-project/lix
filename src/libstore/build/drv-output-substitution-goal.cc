#include "drv-output-substitution-goal.hh"
#include "build-result.hh"
#include "finally.hh"
#include "worker.hh"
#include "substitution-goal.hh"
#include "signals.hh"
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
    state = &DrvOutputSubstitutionGoal::init;
    name = fmt("substitution of '%s'", id.to_string());
    trace("created");
}


kj::Promise<Result<Goal::WorkResult>> DrvOutputSubstitutionGoal::init(bool inBuildSlot) noexcept
try {
    trace("init");

    /* If the derivation already exists, we’re done */
    if (worker.store.queryRealisation(id)) {
        return {Finished{ecSuccess, std::move(buildResult)}};
    }

    subs = settings.useSubstitutes ? getDefaultSubstituters() : std::list<ref<Store>>();
    return tryNext(inBuildSlot);
} catch (...) {
    return {std::current_exception()};
}

kj::Promise<Result<Goal::WorkResult>> DrvOutputSubstitutionGoal::tryNext(bool inBuildSlot) noexcept
try {
    trace("trying next substituter");

    if (!inBuildSlot) {
        return worker.substitutions.acquire().then([this](auto token) {
            slotToken = std::move(token);
            return work();
        });
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
        return {Finished{substituterFailed ? ecFailed : ecNoSubstituters, std::move(buildResult)}};
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

    state = &DrvOutputSubstitutionGoal::realisationFetched;
    return pipe.promise.then([]() -> Result<WorkResult> { return ContinueImmediately{}; });
} catch (...) {
    return {std::current_exception()};
}

kj::Promise<Result<Goal::WorkResult>> DrvOutputSubstitutionGoal::realisationFetched(bool inBuildSlot) noexcept
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
        return tryNext(inBuildSlot);
    }

    kj::Vector<std::pair<GoalPtr, kj::Promise<void>>> dependencies;
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
                return tryNext(inBuildSlot);
            }
            dependencies.add(worker.goalFactory().makeDrvOutputSubstitutionGoal(depId));
        }
    }

    dependencies.add(worker.goalFactory().makePathSubstitutionGoal(outputInfo->outPath));

    if (dependencies.empty()) {
        return outPathValid(inBuildSlot);
    } else {
        state = &DrvOutputSubstitutionGoal::outPathValid;
        return waitForGoals(dependencies.releaseAsArray());
    }
} catch (...) {
    return {std::current_exception()};
}

kj::Promise<Result<Goal::WorkResult>> DrvOutputSubstitutionGoal::outPathValid(bool inBuildSlot) noexcept
try {
    assert(outputInfo);
    trace("output path substituted");

    if (nrFailed > 0) {
        debug("The output path of the derivation output '%s' could not be substituted", id.to_string());
        return {Finished{
            nrNoSubstituters > 0 || nrIncompleteClosure > 0 ? ecIncompleteClosure : ecFailed,
            std::move(buildResult),
        }};
    }

    worker.store.registerDrvOutput(*outputInfo);
    return finished();
} catch (...) {
    return {std::current_exception()};
}

kj::Promise<Result<Goal::WorkResult>> DrvOutputSubstitutionGoal::finished() noexcept
try {
    trace("finished");
    return {Finished{ecSuccess, std::move(buildResult)}};
} catch (...) {
    return {std::current_exception()};
}

kj::Promise<Result<Goal::WorkResult>> DrvOutputSubstitutionGoal::work() noexcept
{
    return (this->*state)(slotToken.valid());
}


}
