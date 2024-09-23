#include "charptr-cast.hh"
#include "worker.hh"
#include "finally.hh"
#include "substitution-goal.hh"
#include "drv-output-substitution-goal.hh"
#include "local-derivation-goal.hh"
#include "signals.hh"
#include "hook-instance.hh" // IWYU pragma: keep

namespace nix {

namespace {
struct ErrorHandler : kj::TaskSet::ErrorHandler
{
    void taskFailed(kj::Exception && e) override
    {
        printError("unexpected async failure in Worker: %s", kj::str(e).cStr());
        abort();
    }
} errorHandler;
}

Worker::Worker(Store & store, Store & evalStore, kj::AsyncIoContext & aio)
    : act(*logger, actRealise)
    , actDerivations(*logger, actBuilds)
    , actSubstitutions(*logger, actCopyPaths)
    , store(store)
    , evalStore(evalStore)
    , aio(aio)
      /* Make sure that we are always allowed to run at least one substitution.
         This prevents infinite waiting. */
    , substitutions(std::max<unsigned>(1, settings.maxSubstitutionJobs))
    , localBuilds(settings.maxBuildJobs)
    , children(errorHandler)
{
    /* Debugging: prevent recursive workers. */
}


Worker::~Worker()
{
    /* Explicitly get rid of all strong pointers now.  After this all
       goals that refer to this worker should be gone.  (Otherwise we
       are in trouble, since goals may call childTerminated() etc. in
       their destructors). */
    topGoals.clear();
    children.clear();

    assert(expectedSubstitutions == 0);
    assert(expectedDownloadSize == 0);
    assert(expectedNarSize == 0);
}


std::pair<std::shared_ptr<DerivationGoal>, kj::Promise<void>> Worker::makeDerivationGoalCommon(
    const StorePath & drvPath,
    const OutputsSpec & wantedOutputs,
    std::function<std::unique_ptr<DerivationGoal>()> mkDrvGoal)
{
    auto & goal_weak = derivationGoals[drvPath];
    std::shared_ptr<DerivationGoal> goal = goal_weak.goal.lock();
    if (!goal) {
        goal = mkDrvGoal();
        goal->notify = std::move(goal_weak.fulfiller);
        goal_weak.goal = goal;
        wakeUp(goal);
    } else {
        goal->addWantedOutputs(wantedOutputs);
    }
    return {goal, goal_weak.promise->addBranch()};
}


std::pair<std::shared_ptr<DerivationGoal>, kj::Promise<void>> Worker::makeDerivationGoal(
    const StorePath & drvPath, const OutputsSpec & wantedOutputs, BuildMode buildMode
)
{
    return makeDerivationGoalCommon(
        drvPath,
        wantedOutputs,
        [&]() -> std::unique_ptr<DerivationGoal> {
            return !dynamic_cast<LocalStore *>(&store)
                ? std::make_unique<DerivationGoal>(
                    drvPath, wantedOutputs, *this, running, buildMode
                )
                : LocalDerivationGoal::makeLocalDerivationGoal(
                    drvPath, wantedOutputs, *this, running, buildMode
                );
        }
    );
}


std::pair<std::shared_ptr<DerivationGoal>, kj::Promise<void>> Worker::makeBasicDerivationGoal(
    const StorePath & drvPath,
    const BasicDerivation & drv,
    const OutputsSpec & wantedOutputs,
    BuildMode buildMode
)
{
    return makeDerivationGoalCommon(
        drvPath,
        wantedOutputs,
        [&]() -> std::unique_ptr<DerivationGoal> {
            return !dynamic_cast<LocalStore *>(&store)
                ? std::make_unique<DerivationGoal>(
                    drvPath, drv, wantedOutputs, *this, running, buildMode
                )
                : LocalDerivationGoal::makeLocalDerivationGoal(
                    drvPath, drv, wantedOutputs, *this, running, buildMode
                );
        }
    );
}


std::pair<std::shared_ptr<PathSubstitutionGoal>, kj::Promise<void>>
Worker::makePathSubstitutionGoal(
    const StorePath & path, RepairFlag repair, std::optional<ContentAddress> ca
)
{
    auto & goal_weak = substitutionGoals[path];
    auto goal = goal_weak.goal.lock(); // FIXME
    if (!goal) {
        goal = std::make_shared<PathSubstitutionGoal>(path, *this, running, repair, ca);
        goal->notify = std::move(goal_weak.fulfiller);
        goal_weak.goal = goal;
        wakeUp(goal);
    }
    return {goal, goal_weak.promise->addBranch()};
}


std::pair<std::shared_ptr<DrvOutputSubstitutionGoal>, kj::Promise<void>>
Worker::makeDrvOutputSubstitutionGoal(
    const DrvOutput & id, RepairFlag repair, std::optional<ContentAddress> ca
)
{
    auto & goal_weak = drvOutputSubstitutionGoals[id];
    auto goal = goal_weak.goal.lock(); // FIXME
    if (!goal) {
        goal = std::make_shared<DrvOutputSubstitutionGoal>(id, *this, running, repair, ca);
        goal->notify = std::move(goal_weak.fulfiller);
        goal_weak.goal = goal;
        wakeUp(goal);
    }
    return {goal, goal_weak.promise->addBranch()};
}


std::pair<GoalPtr, kj::Promise<void>> Worker::makeGoal(const DerivedPath & req, BuildMode buildMode)
{
    return std::visit(overloaded {
        [&](const DerivedPath::Built & bfd) -> std::pair<GoalPtr, kj::Promise<void>> {
            if (auto bop = std::get_if<DerivedPath::Opaque>(&*bfd.drvPath))
                return makeDerivationGoal(bop->path, bfd.outputs, buildMode);
            else
                throw UnimplementedError("Building dynamic derivations in one shot is not yet implemented.");
        },
        [&](const DerivedPath::Opaque & bo) -> std::pair<GoalPtr, kj::Promise<void>> {
            return makePathSubstitutionGoal(bo.path, buildMode == bmRepair ? Repair : NoRepair);
        },
    }, req.raw());
}


template<typename G>
static void removeGoal(std::shared_ptr<G> goal, auto & goalMap)
{
    /* !!! inefficient */
    for (auto i = goalMap.begin();
         i != goalMap.end(); )
        if (i->second.goal.lock() == goal) {
            auto j = i; ++j;
            goalMap.erase(i);
            i = j;
        }
        else ++i;
}


void Worker::goalFinished(GoalPtr goal, Goal::Finished & f)
{
    goal->trace("done");
    assert(!goal->exitCode.has_value());
    goal->exitCode = f.exitCode;
    goal->ex = f.ex;

    permanentFailure |= f.permanentFailure;
    timedOut |= f.timedOut;
    hashMismatch |= f.hashMismatch;
    checkMismatch |= f.checkMismatch;

    removeGoal(goal);
    goal->notify->fulfill();
    goal->cleanup();
}

void Worker::handleWorkResult(GoalPtr goal, Goal::WorkResult how)
{
    std::visit(
        overloaded{
            [&](Goal::StillAlive) {},
            [&](Goal::ContinueImmediately) { wakeUp(goal); },
            [&](Goal::Finished & f) { goalFinished(goal, f); },
        },
        how
    );
    updateStatistics();
}

void Worker::removeGoal(GoalPtr goal)
{
    if (auto drvGoal = std::dynamic_pointer_cast<DerivationGoal>(goal))
        nix::removeGoal(drvGoal, derivationGoals);
    else if (auto subGoal = std::dynamic_pointer_cast<PathSubstitutionGoal>(goal))
        nix::removeGoal(subGoal, substitutionGoals);
    else if (auto subGoal = std::dynamic_pointer_cast<DrvOutputSubstitutionGoal>(goal))
        nix::removeGoal(subGoal, drvOutputSubstitutionGoals);
    else
        assert(false);

    if (topGoals.find(goal) != topGoals.end()) {
        topGoals.erase(goal);
        /* If a top-level goal failed, then kill all other goals
           (unless keepGoing was set). */
        if (goal->exitCode == Goal::ecFailed && !settings.keepGoing)
            topGoals.clear();
    }
}


void Worker::wakeUp(GoalPtr goal)
{
    goal->trace("woken up");
    awake.insert(goal);
}


void Worker::childStarted(GoalPtr goal, kj::Promise<Result<Goal::WorkResult>> promise)
{
    children.add(promise
        .then([this, goal](auto result) {
            if (result.has_value()) {
                handleWorkResult(goal, std::move(result.assume_value()));
            } else {
                childException = result.assume_error();
            }
        })
        .attach(Finally{[this, goal] {
            childTerminated(goal);
        }}));
}


void Worker::childTerminated(GoalPtr goal)
{
    if (childFinished) {
        childFinished->fulfill();
    }
}


void Worker::updateStatistics()
{
    // only update progress info while running. this notably excludes updating
    // progress info while destroying, which causes the progress bar to assert
    if (running && statisticsOutdated) {
        actDerivations.progress(
            doneBuilds, expectedBuilds + doneBuilds, runningBuilds, failedBuilds
        );
        actSubstitutions.progress(
            doneSubstitutions,
            expectedSubstitutions + doneSubstitutions,
            runningSubstitutions,
            failedSubstitutions
        );
        act.setExpected(actFileTransfer, expectedDownloadSize + doneDownloadSize);
        act.setExpected(actCopyPath, expectedNarSize + doneNarSize);

        statisticsOutdated = false;
    }
}

std::vector<GoalPtr> Worker::run(std::function<Targets (GoalFactory &)> req)
{
    auto _topGoals = req(goalFactory());

    assert(!running);
    running = true;
    Finally const _stop([&] { running = false; });

    updateStatistics();

    topGoals.clear();
    for (auto & [goal, _promise] : _topGoals) {
        topGoals.insert(goal);
    }

    debug("entered goal loop");

    while (1) {

        checkInterrupt();

        // TODO GC interface?
        if (auto localStore = dynamic_cast<LocalStore *>(&store))
            localStore->autoGC(false);

        /* Call every wake goal (in the ordering established by
           CompareGoalPtrs). */
        while (!awake.empty() && !topGoals.empty()) {
            Goals awake2;
            for (auto & i : awake) {
                GoalPtr goal = i.lock();
                if (goal) awake2.insert(goal);
            }
            awake.clear();
            for (auto & goal : awake2) {
                checkInterrupt();
                auto result = goal->work();
                if (result.poll(aio.waitScope)) {
                    handleWorkResult(goal, result.wait(aio.waitScope).value());
                } else {
                    childStarted(goal, std::move(result));
                }

                if (topGoals.empty()) break; // stuff may have been cancelled
            }
        }

        if (topGoals.empty()) break;

        /* Wait for input. */
        if (!children.isEmpty())
            waitForInput();
        else {
            assert(!awake.empty());
        }

        if (childException) {
            std::rethrow_exception(childException);
        }
    }

    /* If --keep-going is not set, it's possible that the main goal
       exited while some of its subgoals were still active.  But if
       --keep-going *is* set, then they must all be finished now. */
    assert(!settings.keepGoing || awake.empty());
    assert(!settings.keepGoing || children.isEmpty());

    std::vector<GoalPtr> results;
    for (auto & [i, _p] : _topGoals) {
        results.push_back(i);
    }
    return results;
}

void Worker::waitForInput()
{
    printMsg(lvlVomit, "waiting for children");

    auto waitFor = [&]{
        auto pair = kj::newPromiseAndFulfiller<void>();
        this->childFinished = kj::mv(pair.fulfiller);
        return kj::mv(pair.promise);
    }();

    if (settings.minFree.get() != 0) {
        // Periodicallty wake up to see if we need to run the garbage collector.
        waitFor = waitFor.exclusiveJoin(aio.provider->getTimer().afterDelay(10 * kj::SECONDS));
    }

    waitFor.wait(aio.waitScope);
}


unsigned int Worker::failingExitStatus()
{
    // See API docs in header for explanation
    unsigned int mask = 0;
    bool buildFailure = permanentFailure || timedOut || hashMismatch;
    if (buildFailure)
        mask |= 0x04;  // 100
    if (timedOut)
        mask |= 0x01;  // 101
    if (hashMismatch)
        mask |= 0x02;  // 102
    if (checkMismatch) {
        mask |= 0x08;  // 104
    }

    if (mask)
        mask |= 0x60;
    return mask ? mask : 1;
}


bool Worker::pathContentsGood(const StorePath & path)
{
    auto i = pathContentsGoodCache.find(path);
    if (i != pathContentsGoodCache.end()) return i->second;
    printInfo("checking path '%s'...", store.printStorePath(path));
    auto info = store.queryPathInfo(path);
    bool res;
    if (!pathExists(store.printStorePath(path)))
        res = false;
    else {
        HashResult current = hashPath(info->narHash.type, store.printStorePath(path));
        Hash nullHash(HashType::SHA256);
        res = info->narHash == nullHash || info->narHash == current.first;
    }
    pathContentsGoodCache.insert_or_assign(path, res);
    if (!res)
        printError("path '%s' is corrupted or missing!", store.printStorePath(path));
    return res;
}


void Worker::markContentsGood(const StorePath & path)
{
    pathContentsGoodCache.insert_or_assign(path, true);
}

}
