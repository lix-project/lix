#include "lix/libstore/build/worker.hh"
#include "lix/libstore/build/substitution-goal.hh"
#include "lix/libstore/build/derivation-goal.hh"
#include "lix/libstore/local-store.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/strings.hh"

namespace nix {

kj::Promise<Result<void>> Store::buildPaths(const std::vector<DerivedPath> & reqs, BuildMode buildMode, std::shared_ptr<Store> evalStore)
try {
    auto results =
        TRY_AWAIT(processGoals(*this, evalStore ? *evalStore : *this, [&](GoalFactory & gf) {
            Worker::Targets goals;
            for (auto & br : reqs) {
                goals.emplace_back(gf.makeGoal(br, buildMode));
            }
            return goals;
        }));

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

    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::vector<KeyedBuildResult>>> Store::buildPathsWithResults(
    const std::vector<DerivedPath> & reqs,
    BuildMode buildMode,
    std::shared_ptr<Store> evalStore)
try {
    auto goals =
        TRY_AWAIT(processGoals(*this, evalStore ? *evalStore : *this, [&](GoalFactory & gf) {
            Worker::Targets goals;
            for (const auto & req : reqs) {
                goals.emplace_back(gf.makeGoal(req, buildMode));
            }
            return goals;
        })).goals;

    std::vector<KeyedBuildResult> results;

    for (auto && [goalIdx, req] : enumerate(reqs))
        results.emplace_back(goals[goalIdx].result.restrictTo(req));

    co_return results;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<BuildResult>> Store::buildDerivation(const StorePath & drvPath, const BasicDerivation & drv,
    BuildMode buildMode)
try {
    try {
        auto results = TRY_AWAIT(processGoals(*this, *this, [&](GoalFactory & gf) {
            // sometimes clang lints are really annoying. this would be safe without
            // the explicit coroutine param captures, but clang-tidy does not see it
            return [](auto && gf, auto && drvPath, auto && drv, auto buildMode
                   ) -> kj::Promise<Result<Worker::Targets>> {
                try {
                    Worker::Targets goals;
                    goals.emplace_back(TRY_AWAIT(
                        gf.makeBasicDerivationGoal(drvPath, drv, OutputsSpec::All{}, buildMode)
                    ));
                    co_return goals;
                } catch (...) {
                    co_return result::current_exception();
                }
            }(gf, drvPath, drv, buildMode);
        }));
        auto & result = results.goals.begin()->second;
        co_return result.result.restrictTo(DerivedPath::Built {
            .drvPath = makeConstantStorePath(drvPath),
            .outputs = OutputsSpec::All {},
        });
    } catch (Error & e) {
        co_return BuildResult {
            .status = BuildResult::MiscFailure,
            .errorMsg = e.msg(),
        };
    };
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>> Store::ensurePath(const StorePath & path)
try {
    /* If the path is already valid, we're done. */
    if (TRY_AWAIT(isValidPath(path))) co_return result::success();

    auto results = TRY_AWAIT(processGoals(*this, *this, [&](GoalFactory & gf) {
        Worker::Targets goals;
        goals.emplace_back(gf.makePathSubstitutionGoal(path));
        return goals;
    }));
    auto & result = results.goals.begin()->second;

    if (result.exitCode != Goal::ecSuccess) {
        if (result.ex) {
            result.ex->withExitStatus(results.failingExitStatus);
            throw std::move(*result.ex);
        } else
            throw Error(results.failingExitStatus, "path '%s' does not exist and cannot be created", printStorePath(path));
    }

    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>> Store::repairPath(const StorePath & path)
try {
    auto results = TRY_AWAIT(processGoals(*this, *this, [&](GoalFactory & gf) {
        Worker::Targets goals;
        goals.emplace_back(gf.makePathSubstitutionGoal(path, Repair));
        return goals;
    }));
    auto & result = results.goals.begin()->second;

    if (result.exitCode != Goal::ecSuccess) {
        /* Since substituting the path didn't work, if we have a valid
           deriver, then rebuild the deriver. */
        auto info = TRY_AWAIT(queryPathInfo(path));
        if (info->deriver && TRY_AWAIT(isValidPath(*info->deriver))) {
            TRY_AWAIT(processGoals(*this, *this, [&](GoalFactory & gf) {
                Worker::Targets goals;
                goals.emplace_back(gf.makeGoal(
                    DerivedPath::Built{
                        .drvPath = makeConstantStorePath(*info->deriver),
                        // FIXME: Should just build the specific output we need.
                        .outputs = OutputsSpec::All{},
                    },
                    bmRepair
                ));
                return goals;
            }));
        } else
            throw Error(
                results.failingExitStatus,
                "cannot repair path '%s'",
                toRealPath(printStorePath(path))
            );
    }

    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

}
