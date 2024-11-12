#include "lix/libstore/build/worker.hh"
#include "lix/libstore/build/substitution-goal.hh"
#include "lix/libstore/build/derivation-goal.hh"
#include "lix/libstore/local-store.hh"
#include "lix/libutil/strings.hh"

namespace nix {

void Store::buildPaths(const std::vector<DerivedPath> & reqs, BuildMode buildMode, std::shared_ptr<Store> evalStore)
{
    auto aio = kj::setupAsyncIo();

    auto results = processGoals(*this, evalStore ? *evalStore : *this, aio, [&](GoalFactory & gf) {
        Worker::Targets goals;
        for (auto & br : reqs)
            goals.emplace_back(gf.makeGoal(br, buildMode));
        return goals;
    }).wait(aio.waitScope).value();

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
            if (result.storePath)
                failed.insert(printStorePath(*result.storePath));
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

    auto goals = processGoals(*this, evalStore ? *evalStore : *this, aio, [&](GoalFactory & gf) {
        Worker::Targets goals;
        for (const auto & req : reqs) {
            goals.emplace_back(gf.makeGoal(req, buildMode));
        }
        return goals;
    }).wait(aio.waitScope).value().goals;

    std::vector<KeyedBuildResult> results;

    for (auto && [goalIdx, req] : enumerate(reqs))
        results.emplace_back(goals[goalIdx].result.restrictTo(req));

    return results;
}

BuildResult Store::buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
    BuildMode buildMode)
{
    auto aio = kj::setupAsyncIo();

    try {
        auto results = processGoals(*this, *this, aio, [&](GoalFactory & gf) {
            Worker::Targets goals;
            goals.emplace_back(gf.makeBasicDerivationGoal(drvPath, drv, OutputsSpec::All{}, buildMode));
            return goals;
        }).wait(aio.waitScope).value();
        auto & result = results.goals.begin()->second;
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

    auto results = processGoals(*this, *this, aio, [&](GoalFactory & gf) {
        Worker::Targets goals;
        goals.emplace_back(gf.makePathSubstitutionGoal(path));
        return goals;
    }).wait(aio.waitScope).value();
    auto & result = results.goals.begin()->second;

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

    auto results = processGoals(*this, *this, aio, [&](GoalFactory & gf) {
        Worker::Targets goals;
        goals.emplace_back(gf.makePathSubstitutionGoal(path, Repair));
        return goals;
    }).wait(aio.waitScope).value();
    auto & result = results.goals.begin()->second;

    if (result.exitCode != Goal::ecSuccess) {
        /* Since substituting the path didn't work, if we have a valid
           deriver, then rebuild the deriver. */
        auto info = queryPathInfo(path);
        if (info->deriver && isValidPath(*info->deriver)) {
            processGoals(*this, *this, aio, [&](GoalFactory & gf) {
                Worker::Targets goals;
                goals.emplace_back(gf.makeGoal(
                    DerivedPath::Built{
                        .drvPath = makeConstantStorePathRef(*info->deriver),
                        // FIXME: Should just build the specific output we need.
                        .outputs = OutputsSpec::All{},
                    },
                    bmRepair
                ));
                return goals;
            }).wait(aio.waitScope).value();
        } else
            throw Error(results.failingExitStatus, "cannot repair path '%s'", printStorePath(path));
    }
}

}
