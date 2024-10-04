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


template<typename ID, std::derived_from<Goal> G>
std::pair<std::shared_ptr<G>, kj::Promise<void>> Worker::makeGoalCommon(
    std::map<ID, CachedGoal<G>> & map,
    const ID & key,
    InvocableR<std::unique_ptr<G>> auto create,
    InvocableR<bool, G &> auto modify
)
{
    auto [it, _inserted] = map.try_emplace(key);
    // try twice to create the goal. we can only loop if we hit the continue,
    // and then we only want to recreate the goal *once*. concurrent accesses
    // to the worker are not sound, we want to catch them if at all possible.
    for ([[maybe_unused]] auto _attempt : {1, 2}) {
        auto & goal_weak = it->second;
        auto goal = goal_weak.goal.lock();
        if (!goal) {
            goal = create();
            goal->notify = std::move(goal_weak.fulfiller);
            goal_weak.goal = goal;
            // do not start working immediately. if we are not yet running we
            // may create dependencies as though they were toplevel goals, in
            // which case the dependencies will not report build errors. when
            // we are running we may be called for this same goal more times,
            // and then we want to modify rather than recreate when possible.
            childStarted(goal, kj::evalLater([goal] { return goal->work(); }));
        } else {
            if (!modify(*goal)) {
                goal_weak = {};
                continue;
            }
        }
        return {goal, goal_weak.promise->addBranch()};
    }
    assert(false && "could not make a goal. possible concurrent worker access");
}


std::pair<std::shared_ptr<DerivationGoal>, kj::Promise<void>> Worker::makeDerivationGoal(
    const StorePath & drvPath, const OutputsSpec & wantedOutputs, BuildMode buildMode
)
{
    return makeGoalCommon(
        derivationGoals,
        drvPath,
        [&]() -> std::unique_ptr<DerivationGoal> {
            return !dynamic_cast<LocalStore *>(&store)
                ? std::make_unique<DerivationGoal>(
                    drvPath, wantedOutputs, *this, running, buildMode
                )
                : LocalDerivationGoal::makeLocalDerivationGoal(
                    drvPath, wantedOutputs, *this, running, buildMode
                );
        },
        [&](DerivationGoal & g) { return g.addWantedOutputs(wantedOutputs); }
    );
}


std::pair<std::shared_ptr<DerivationGoal>, kj::Promise<void>> Worker::makeBasicDerivationGoal(
    const StorePath & drvPath,
    const BasicDerivation & drv,
    const OutputsSpec & wantedOutputs,
    BuildMode buildMode
)
{
    return makeGoalCommon(
        derivationGoals,
        drvPath,
        [&]() -> std::unique_ptr<DerivationGoal> {
            return !dynamic_cast<LocalStore *>(&store)
                ? std::make_unique<DerivationGoal>(
                    drvPath, drv, wantedOutputs, *this, running, buildMode
                )
                : LocalDerivationGoal::makeLocalDerivationGoal(
                    drvPath, drv, wantedOutputs, *this, running, buildMode
                );
        },
        [&](DerivationGoal & g) { return g.addWantedOutputs(wantedOutputs); }
    );
}


std::pair<std::shared_ptr<PathSubstitutionGoal>, kj::Promise<void>>
Worker::makePathSubstitutionGoal(
    const StorePath & path, RepairFlag repair, std::optional<ContentAddress> ca
)
{
    return makeGoalCommon(
        substitutionGoals,
        path,
        [&] { return std::make_unique<PathSubstitutionGoal>(path, *this, running, repair, ca); },
        [&](auto &) { return true; }
    );
}


std::pair<std::shared_ptr<DrvOutputSubstitutionGoal>, kj::Promise<void>>
Worker::makeDrvOutputSubstitutionGoal(
    const DrvOutput & id, RepairFlag repair, std::optional<ContentAddress> ca
)
{
    return makeGoalCommon(
        drvOutputSubstitutionGoals,
        id,
        [&] { return std::make_unique<DrvOutputSubstitutionGoal>(id, *this, running, repair, ca); },
        [&](auto &) { return true; }
    );
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


void Worker::goalFinished(GoalPtr goal, Goal::WorkResult & f)
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


void Worker::childStarted(GoalPtr goal, kj::Promise<Result<Goal::WorkResult>> promise)
{
    children.add(promise
        .then([this, goal](auto result) {
            if (result.has_value()) {
                goalFinished(goal, result.assume_value());
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


kj::Promise<Result<void>> Worker::updateStatistics()
try {
    while (true) {
        statisticsUpdateInhibitor = co_await statisticsUpdateSignal.acquire();

        // only update progress info while running. this notably excludes updating
        // progress info while destroying, which causes the progress bar to assert
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

        // limit to 50fps. that should be more than good enough for anything we do
        co_await aio.provider->getTimer().afterDelay(20 * kj::MILLISECONDS);
    }
} catch (...) {
    co_return result::failure(std::current_exception());
}

std::vector<GoalPtr> Worker::run(std::function<Targets (GoalFactory &)> req)
{
    auto _topGoals = req(goalFactory());

    assert(!running);
    running = true;
    Finally const _stop([&] { running = false; });

    topGoals.clear();
    for (auto & [goal, _promise] : _topGoals) {
        topGoals.insert(goal);
    }

    auto onInterrupt = kj::newPromiseAndCrossThreadFulfiller<Result<void>>();
    auto interruptCallback = createInterruptCallback([&] {
        return result::failure(std::make_exception_ptr(makeInterrupted()));
    });

    auto promise =
        runImpl().exclusiveJoin(updateStatistics()).exclusiveJoin(std::move(onInterrupt.promise));

    // TODO GC interface?
    if (auto localStore = dynamic_cast<LocalStore *>(&store); localStore && settings.minFree != 0) {
        // Periodically wake up to see if we need to run the garbage collector.
        promise = promise.exclusiveJoin(boopGC(*localStore));
    }

    promise.wait(aio.waitScope).value();

    std::vector<GoalPtr> results;
    for (auto & [i, _p] : _topGoals) {
        results.push_back(i);
    }
    return results;
}

kj::Promise<Result<void>> Worker::runImpl()
try {
    debug("entered goal loop");

    while (1) {
        if (topGoals.empty()) break;

        /* Wait for input. */
        if (!children.isEmpty())
            (co_await waitForInput()).value();

        if (childException) {
            std::rethrow_exception(childException);
        }
    }

    /* If --keep-going is not set, it's possible that the main goal
       exited while some of its subgoals were still active.  But if
       --keep-going *is* set, then they must all be finished now. */
    assert(!settings.keepGoing || children.isEmpty());

    co_return result::success();
} catch (...) {
    co_return result::failure(std::current_exception());
}

kj::Promise<Result<void>> Worker::boopGC(LocalStore & localStore)
try {
    while (true) {
        co_await aio.provider->getTimer().afterDelay(10 * kj::SECONDS);
        localStore.autoGC(false);
    }
} catch (...) {
    co_return result::failure(std::current_exception());
}

kj::Promise<Result<void>> Worker::waitForInput()
try {
    printMsg(lvlVomit, "waiting for children");

    auto pair = kj::newPromiseAndFulfiller<void>();
    this->childFinished = kj::mv(pair.fulfiller);
    co_await pair.promise;
    co_return result::success();
} catch (...) {
    co_return result::failure(std::current_exception());
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
