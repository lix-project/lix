#include "async-collect.hh"
#include "charptr-cast.hh"
#include "worker.hh"
#include "finally.hh"
#include "substitution-goal.hh"
#include "drv-output-substitution-goal.hh"
#include "local-derivation-goal.hh"
#include "signals.hh"
#include "hook-instance.hh" // IWYU pragma: keep
#include <boost/outcome/try.hpp>
#include <kj/vector.h>

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
    children.clear();

    derivationGoals.clear();
    drvOutputSubstitutionGoals.clear();
    substitutionGoals.clear();

    assert(expectedSubstitutions == 0);
    assert(expectedDownloadSize == 0);
    assert(expectedNarSize == 0);
}


template<typename ID, std::derived_from<Goal> G>
std::pair<std::shared_ptr<G>, kj::Promise<Result<Goal::WorkResult>>> Worker::makeGoalCommon(
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
        auto & cachedGoal = it->second;
        auto & goal = cachedGoal.goal;
        if (!goal) {
            goal = create();
            // do not start working immediately. if we are not yet running we
            // may create dependencies as though they were toplevel goals, in
            // which case the dependencies will not report build errors. when
            // we are running we may be called for this same goal more times,
            // and then we want to modify rather than recreate when possible.
            auto removeWhenDone = [goal, &map, it] {
                // c++ lambda coroutine capture semantics are *so* fucked up.
                return [](auto goal, auto & map, auto it) -> kj::Promise<Result<Goal::WorkResult>> {
                    auto result = co_await goal->work();
                    // a concurrent call to makeGoalCommon may have reset our
                    // cached goal and replaced it with a new instance. don't
                    // remove the goal in this case, otherwise we will crash.
                    if (goal == it->second.goal) {
                        map.erase(it);
                    }
                    co_return result;
                }(goal, map, it);
            };
            cachedGoal.promise = kj::evalLater(std::move(removeWhenDone)).fork();
            children.add(cachedGoal.promise.addBranch().then([this](auto _result) {
                if (_result.has_value()) {
                    auto & result = _result.value();
                    permanentFailure |= result.permanentFailure;
                    timedOut |= result.timedOut;
                    hashMismatch |= result.hashMismatch;
                    checkMismatch |= result.checkMismatch;
                }
            }));
        } else {
            if (!modify(*goal)) {
                cachedGoal = {};
                continue;
            }
        }
        return {goal, cachedGoal.promise.addBranch()};
    }
    assert(false && "could not make a goal. possible concurrent worker access");
}


std::pair<std::shared_ptr<DerivationGoal>, kj::Promise<Result<Goal::WorkResult>>> Worker::makeDerivationGoal(
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


std::pair<std::shared_ptr<DerivationGoal>, kj::Promise<Result<Goal::WorkResult>>> Worker::makeBasicDerivationGoal(
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


std::pair<std::shared_ptr<PathSubstitutionGoal>, kj::Promise<Result<Goal::WorkResult>>>
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


std::pair<std::shared_ptr<DrvOutputSubstitutionGoal>, kj::Promise<Result<Goal::WorkResult>>>
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


std::pair<GoalPtr, kj::Promise<Result<Goal::WorkResult>>> Worker::makeGoal(const DerivedPath & req, BuildMode buildMode)
{
    return std::visit(overloaded {
        [&](const DerivedPath::Built & bfd) -> std::pair<GoalPtr, kj::Promise<Result<Goal::WorkResult>>> {
            if (auto bop = std::get_if<DerivedPath::Opaque>(&*bfd.drvPath))
                return makeDerivationGoal(bop->path, bfd.outputs, buildMode);
            else
                throw UnimplementedError("Building dynamic derivations in one shot is not yet implemented.");
        },
        [&](const DerivedPath::Opaque & bo) -> std::pair<GoalPtr, kj::Promise<Result<Goal::WorkResult>>> {
            return makePathSubstitutionGoal(bo.path, buildMode == bmRepair ? Repair : NoRepair);
        },
    }, req.raw());
}

kj::Promise<Result<Worker::Results>> Worker::updateStatistics()
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

Worker::Results Worker::run(std::function<Targets (GoalFactory &)> req)
{
    auto topGoals = req(goalFactory());

    assert(!running);
    running = true;
    Finally const _stop([&] { running = false; });

    auto onInterrupt = kj::newPromiseAndCrossThreadFulfiller<Result<Results>>();
    auto interruptCallback = createInterruptCallback([&] {
        onInterrupt.fulfiller->fulfill(result::failure(std::make_exception_ptr(makeInterrupted())));
    });

    auto promise = runImpl(std::move(topGoals))
                       .exclusiveJoin(updateStatistics())
                       .exclusiveJoin(std::move(onInterrupt.promise));

    // TODO GC interface?
    if (auto localStore = dynamic_cast<LocalStore *>(&store); localStore && settings.minFree != 0u) {
        // Periodically wake up to see if we need to run the garbage collector.
        promise = promise.exclusiveJoin(boopGC(*localStore));
    }

    return promise.wait(aio.waitScope).value();
}

kj::Promise<Result<Worker::Results>> Worker::runImpl(Targets topGoals)
try {
    debug("entered goal loop");

    kj::Vector<Targets::value_type> promises(topGoals.size());
    for (auto & gp : topGoals) {
        promises.add(std::move(gp));
    }

    Results results;

    auto collect = AsyncCollect(promises.releaseAsArray());
    while (auto done = co_await collect.next()) {
        // propagate goal exceptions outward
        BOOST_OUTCOME_CO_TRY(auto result, done->second);
        results.emplace(done->first, result);

        /* If a top-level goal failed, then kill all other goals
           (unless keepGoing was set). */
        if (result.exitCode == Goal::ecFailed && !settings.keepGoing) {
            children.clear();
            break;
        }
    }

    /* If --keep-going is not set, it's possible that the main goal
       exited while some of its subgoals were still active.  But if
       --keep-going *is* set, then they must all be finished now. */
    assert(!settings.keepGoing || children.isEmpty());

    co_return std::move(results);
} catch (...) {
    co_return result::failure(std::current_exception());
}

kj::Promise<Result<Worker::Results>> Worker::boopGC(LocalStore & localStore)
try {
    while (true) {
        co_await aio.provider->getTimer().afterDelay(10 * kj::SECONDS);
        localStore.autoGC(false);
    }
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
