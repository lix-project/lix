#include "drv-output-substitution-goal.hh"
#include "finally.hh"
#include "worker.hh"
#include "substitution-goal.hh"
#include "signals.hh"

namespace nix {

DrvOutputSubstitutionGoal::DrvOutputSubstitutionGoal(
    const DrvOutput & id,
    Worker & worker,
    RepairFlag repair,
    std::optional<ContentAddress> ca)
    : Goal(worker)
    , id(id)
{
    state = &DrvOutputSubstitutionGoal::init;
    name = fmt("substitution of '%s'", id.to_string());
    trace("created");
}


Goal::WorkResult DrvOutputSubstitutionGoal::init(bool inBuildSlot)
{
    trace("init");

    /* If the derivation already exists, we’re done */
    if (worker.store.queryRealisation(id)) {
        return Finished{ecSuccess};
    }

    subs = settings.useSubstitutes ? getDefaultSubstituters() : std::list<ref<Store>>();
    return tryNext(inBuildSlot);
}

Goal::WorkResult DrvOutputSubstitutionGoal::tryNext(bool inBuildSlot)
{
    trace("trying next substituter");

    if (!inBuildSlot) {
        return WaitForSlot{};
    }

    maintainRunningSubstitutions =
        std::make_unique<MaintainCount<uint64_t>>(worker.runningSubstitutions);

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
        return Finished{substituterFailed ? ecFailed : ecNoSubstituters};
    }

    sub = subs.front();
    subs.pop_front();

    /* The async call to a curl download below can outlive `this` (if
       some other error occurs), so it must not touch `this`. So put
       the shared state in a separate refcounted object. */
    downloadState = std::make_shared<DownloadState>();
    downloadState->outPipe.create();

    downloadState->result =
        std::async(std::launch::async, [downloadState{downloadState}, id{id}, sub{sub}] {
            ReceiveInterrupts receiveInterrupts;
            Finally updateStats([&]() { downloadState->outPipe.writeSide.close(); });
            return sub->queryRealisation(id);
        });

    state = &DrvOutputSubstitutionGoal::realisationFetched;
    return WaitForWorld{{downloadState->outPipe.readSide.get()}, true};
}

Goal::WorkResult DrvOutputSubstitutionGoal::realisationFetched(bool inBuildSlot)
{
    worker.childTerminated(this);
    maintainRunningSubstitutions.reset();

    try {
        outputInfo = downloadState->result.get();
    } catch (std::exception & e) {
        printError(e.what());
        substituterFailed = true;
    }

    if (!outputInfo) {
        return tryNext(inBuildSlot);
    }

    WaitForGoals result;
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
            result.goals.insert(worker.makeDrvOutputSubstitutionGoal(depId));
        }
    }

    result.goals.insert(worker.makePathSubstitutionGoal(outputInfo->outPath));

    if (result.goals.empty()) {
        return outPathValid(inBuildSlot);
    } else {
        state = &DrvOutputSubstitutionGoal::outPathValid;
        return result;
    }
}

Goal::WorkResult DrvOutputSubstitutionGoal::outPathValid(bool inBuildSlot)
{
    assert(outputInfo);
    trace("output path substituted");

    if (nrFailed > 0) {
        debug("The output path of the derivation output '%s' could not be substituted", id.to_string());
        return Finished{
            nrNoSubstituters > 0 || nrIncompleteClosure > 0 ? ecIncompleteClosure : ecFailed
        };
    }

    worker.store.registerDrvOutput(*outputInfo);
    return finished();
}

Goal::WorkResult DrvOutputSubstitutionGoal::finished()
{
    trace("finished");
    return Finished{ecSuccess};
}

std::string DrvOutputSubstitutionGoal::key()
{
    /* "a$" ensures substitution goals happen before derivation
       goals. */
    return "a$" + std::string(id.to_string());
}

Goal::WorkResult DrvOutputSubstitutionGoal::work(bool inBuildSlot)
{
    return (this->*state)(inBuildSlot);
}


}
