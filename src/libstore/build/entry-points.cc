#include "worker.hh"
#include "substitution-goal.hh"
#include "derivation-goal.hh"
#include "local-store.hh"
#include "strings.hh"

namespace nix {

static auto runWorker(Worker & worker, auto mkGoals)
{
    return worker.run(mkGoals);
}

void Store::buildPaths(const std::vector<DerivedPath> & reqs, BuildMode buildMode, std::shared_ptr<Store> evalStore)
{
    auto aio = kj::setupAsyncIo();
    Worker worker(*this, evalStore ? *evalStore : *this, aio);

    auto results = runWorker(worker, [&](GoalFactory & gf) {
        Worker::Targets goals;
        for (auto & br : reqs)
            goals.emplace(gf.makeGoal(br, buildMode));
        return goals;
    });

    StringSet failed;
    std::shared_ptr<Error> ex;
    for (auto & [i, result] : results.goals) {
        if (result.ex) {
            if (ex)
                logError(result.ex->info());
            else
                ex = result.ex;
        }
        if (result.exitCode != Goal::ecSuccess) {
            if (auto i2 = dynamic_cast<DerivationGoal *>(i.get()))
                failed.insert(printStorePath(i2->drvPath));
            else if (auto i2 = dynamic_cast<PathSubstitutionGoal *>(i.get()))
                failed.insert(printStorePath(i2->storePath));
        }
    }

    if (failed.size() == 1 && ex) {
        ex->withExitStatus(results.failingExitStatus);
        throw std::move(*ex);
    } else if (!failed.empty()) {
        if (ex) logError(ex->info());
        throw Error(results.failingExitStatus, "build of %s failed", concatStringsSep(", ", quoteStrings(failed)));
    }
}

std::vector<KeyedBuildResult> Store::buildPathsWithResults(
    const std::vector<DerivedPath> & reqs,
    BuildMode buildMode,
    std::shared_ptr<Store> evalStore)
{
    auto aio = kj::setupAsyncIo();
    Worker worker(*this, evalStore ? *evalStore : *this, aio);

    std::vector<std::pair<const DerivedPath &, GoalPtr>> state;

    auto goals = runWorker(worker, [&](GoalFactory & gf) {
        Worker::Targets goals;
        for (const auto & req : reqs) {
            auto goal = gf.makeGoal(req, buildMode);
            state.push_back({req, goal.first});
            goals.emplace(std::move(goal));
        }
        return goals;
    }).goals;

    std::vector<KeyedBuildResult> results;

    for (auto & [req, goalPtr] : state)
        results.emplace_back(goals[goalPtr].result.restrictTo(req));

    return results;
}

BuildResult Store::buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
    BuildMode buildMode)
{
    auto aio = kj::setupAsyncIo();
    Worker worker(*this, *this, aio);

    try {
        auto results = runWorker(worker, [&](GoalFactory & gf) {
            Worker::Targets goals;
            goals.emplace(gf.makeBasicDerivationGoal(drvPath, drv, OutputsSpec::All{}, buildMode));
            return goals;
        });
        auto [goal, result] = *results.goals.begin();
        return result.result.restrictTo(DerivedPath::Built {
            .drvPath = makeConstantStorePathRef(drvPath),
            .outputs = OutputsSpec::All {},
        });
    } catch (Error & e) {
        return BuildResult {
            .status = BuildResult::MiscFailure,
            .errorMsg = e.msg(),
        };
    };
}


void Store::ensurePath(const StorePath & path)
{
    /* If the path is already valid, we're done. */
    if (isValidPath(path)) return;

    auto aio = kj::setupAsyncIo();
    Worker worker(*this, *this, aio);

    auto results = runWorker(worker, [&](GoalFactory & gf) {
        Worker::Targets goals;
        goals.emplace(gf.makePathSubstitutionGoal(path));
        return goals;
    });
    auto [goal, result] = *results.goals.begin();

    if (result.exitCode != Goal::ecSuccess) {
        if (result.ex) {
            result.ex->withExitStatus(results.failingExitStatus);
            throw std::move(*result.ex);
        } else
            throw Error(results.failingExitStatus, "path '%s' does not exist and cannot be created", printStorePath(path));
    }
}


void Store::repairPath(const StorePath & path)
{
    auto aio = kj::setupAsyncIo();
    Worker worker(*this, *this, aio);

    auto results = runWorker(worker, [&](GoalFactory & gf) {
        Worker::Targets goals;
        goals.emplace(gf.makePathSubstitutionGoal(path, Repair));
        return goals;
    });
    auto [goal, result] = *results.goals.begin();

    if (result.exitCode != Goal::ecSuccess) {
        /* Since substituting the path didn't work, if we have a valid
           deriver, then rebuild the deriver. */
        auto info = queryPathInfo(path);
        if (info->deriver && isValidPath(*info->deriver)) {
            worker.run([&](GoalFactory & gf) {
                Worker::Targets goals;
                goals.emplace(gf.makeGoal(
                    DerivedPath::Built{
                        .drvPath = makeConstantStorePathRef(*info->deriver),
                        // FIXME: Should just build the specific output we need.
                        .outputs = OutputsSpec::All{},
                    },
                    bmRepair
                ));
                return goals;
            });
        } else
            throw Error(results.failingExitStatus, "cannot repair path '%s'", printStorePath(path));
    }
}

}
