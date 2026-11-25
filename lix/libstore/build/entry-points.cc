#include "lix/libstore/build/worker.hh"
#include "lix/libstore/build/substitution-goal.hh"
#include "lix/libstore/build/derivation-goal.hh"
#include "lix/libstore/build-store.hh"
#include "lix/libstore/local-store.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/strings.hh"

namespace nix {

/**
 * The exit status in case of failure.
 *
 * In the case of a build failure, returned value follows this
 * bitmask:
 *
 * ```
 * 0b1100100
 *      ^^^^
 *      |||`- timeout
 *      ||`-- output hash mismatch
 *      |`--- build failure
 *      `---- not deterministic
 * ```
 *
 * In other words, the failure code is at least 100 (0b1100100), but
 * might also be greater.
 *
 * Otherwise (no build failure, but some other sort of failure by
 * assumption), this returned value is 1.
 */
static unsigned int failingExitStatus(Worker::Results & results)
{
    unsigned int mask = 0;
    bool buildFailure = results.permanentFailure || results.timedOut || results.hashMismatch;
    if (buildFailure) {
        mask |= 0x04; // 100
    }
    if (results.timedOut) {
        mask |= 0x01; // 101
    }
    if (results.hashMismatch) {
        mask |= 0x02; // 102
    }
    if (results.checkMismatch) {
        mask |= 0x08; // 104
    }

    if (mask) {
        mask |= 0x60;
    }
    return mask ? mask : 1;
}

static kj::Promise<Result<void>>
copyDrvsFromEvalStore(const std::vector<DerivedPath> & paths, Store * evalStore, Store & to)
try {
    if (evalStore && evalStore != &to) {
        // the remote doesn't have a way to access evalStore, so copy the derivation closure.
        // ths includes all input sources and dependency derivations that will never be built
        // on the building store (for example because they can be substituted, or are already
        // present). we consider this a correctness advantage despite some inefficienty since
        // then for all output paths we also have full and correct deriver information in the
        // building store, not just ephemeral derivations that cause outputs paths to appear.
        StorePathSet drvPaths;
        for (const auto & i : paths) {
            std::visit(
                overloaded{
                    [&](const DerivedPath::Opaque & bp) {
                        // Do nothing, path is hopefully there already
                    },
                    [&](const DerivedPath::Built & bp) { drvPaths.insert(bp.drvPath.path); },
                },
                i.raw()
            );
        }
        StorePathSet validDrvPaths;
        for (const auto & drv : drvPaths) {
            if (TRY_AWAIT(evalStore->isValidPath(drv))) {
                validDrvPaths.insert(drv);
            }
        }
        TRY_AWAIT(copyClosure(*evalStore, to, validDrvPaths));
    }
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> BuildStore::buildPaths(
    const std::vector<DerivedPath> & reqs, BuildMode buildMode, std::shared_ptr<Store> evalStore
)
try {
    TRY_AWAIT(copyDrvsFromEvalStore(reqs, evalStore.get(), *this));
    auto results = TRY_AWAIT(processGoals(*this, [&](GoalFactory & gf) {
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
        ex->withExitStatus(failingExitStatus(results));
        throw std::move(*ex);
    } else if (!failed.empty()) {
        if (ex) logError(ex->info());
        throw Error(
            failingExitStatus(results), "build of %s failed", concatStringsSep(", ", quoteStrings(failed))
        );
    }

    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::vector<KeyedBuildResult>>> BuildStore::buildPathsWithResults(
    const std::vector<DerivedPath> & reqs, BuildMode buildMode, std::shared_ptr<Store> evalStore
)
try {
    TRY_AWAIT(copyDrvsFromEvalStore(reqs, evalStore.get(), *this));
    auto goals = TRY_AWAIT(processGoals(*this, [&](GoalFactory & gf) {
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

kj::Promise<Result<BuildResult>>
BuildStore::buildDerivation(const StorePath & drvPath, const BasicDerivation & drv, BuildMode buildMode)
try {
    try {
        auto results = TRY_AWAIT(processGoals(*this, [&](GoalFactory & gf) {
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

kj::Promise<Result<void>> BuildStore::ensurePath(const StorePath & path)
try {
    auto results = TRY_AWAIT(buildPathsWithResults({{DerivedPathOpaque{path}}}));
    assert(results.size() == 1 && "expected a single result");
    auto & result = results.front();
    if (!result.success()) {
        result.rethrow();
    }

    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> BuildStore::repairPath(const StorePath & path)
try {
    auto results = TRY_AWAIT(processGoals(*this, [&](GoalFactory & gf) {
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
            TRY_AWAIT(processGoals(*this, [&](GoalFactory & gf) {
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
                failingExitStatus(results), "cannot repair path '%s'", toRealPath(printStorePath(path))
            );
    }

    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

}
