#include "lix/libstore/derivations.hh"
#include "lix/libstore/parsed-derivations.hh"
#include "lix/libstore/globals.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/async-collect.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/thread-pool.hh"
#include "lix/libutil/topo-sort.hh"
#include "lix/libutil/closure.hh"
#include "lix/libstore/filetransfer.hh"
#include "lix/libutil/strings.hh"
#include <kj/async.h>
#include <kj/common.h>
#include <kj/vector.h>

namespace nix {

kj::Promise<Result<void>> Store::computeFSClosure(const StorePathSet & startPaths,
    StorePathSet & paths_, bool flipDirection, bool includeOutputs, bool includeDerivers)
try {
    std::function<
        kj::Promise<Result<std::set<StorePath>>>(const StorePath & path, ref<const ValidPathInfo>)>
        queryDeps;
    if (flipDirection) {
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
        queryDeps = [&](const StorePath & path,
                        ref<const ValidPathInfo>) -> kj::Promise<Result<std::set<StorePath>>> {
            try {
                StorePathSet res;
                StorePathSet referrers;
                TRY_AWAIT(queryReferrers(path, referrers));
                for (auto& ref : referrers)
                    if (ref != path)
                        res.insert(ref);

                if (includeOutputs)
                    for (auto& i : TRY_AWAIT(queryValidDerivers(path)))
                        res.insert(i);

                if (includeDerivers && path.isDerivation())
                    for (auto& [_, outPath] : TRY_AWAIT(queryDerivationOutputMap(path)))
                        if (TRY_AWAIT(isValidPath(outPath)))
                            res.insert(outPath);
                co_return res;
            } catch (...) {
                co_return result::current_exception();
            }
        };
    } else {
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
        queryDeps = [&](const StorePath & path,
                        ref<const ValidPathInfo> info) -> kj::Promise<Result<std::set<StorePath>>> {
            try {
                StorePathSet res;
                for (auto& ref : info->references)
                    if (ref != path)
                        res.insert(ref);

                if (includeOutputs && path.isDerivation())
                    for (auto& [_, outPath] : TRY_AWAIT(queryDerivationOutputMap(path)))
                        if (TRY_AWAIT(isValidPath(outPath)))
                            res.insert(outPath);

                if (includeDerivers && info->deriver && TRY_AWAIT(isValidPath(*info->deriver)))
                    res.insert(*info->deriver);
                co_return res;
            } catch (...) {
                co_return result::current_exception();
            }
        };
    }

    paths_.merge(TRY_AWAIT(computeClosureAsync<StorePath>(
        startPaths,
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
        [&](const StorePath& path) -> kj::Promise<Result<StorePathSet>> {
            try {
                co_return TRY_AWAIT(queryDeps(path, TRY_AWAIT(queryPathInfo(path))));
            } catch (...) {
                co_return result::current_exception();
            }
        })));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> Store::computeFSClosure(const StorePath & startPath,
    StorePathSet & paths_, bool flipDirection, bool includeOutputs, bool includeDerivers)
try {
    StorePathSet paths;
    paths.insert(startPath);
    TRY_AWAIT(computeFSClosure(paths, paths_, flipDirection, includeOutputs, includeDerivers));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


const ContentAddress * getDerivationCA(const BasicDerivation & drv)
{
    auto out = drv.outputs.find("out");
    if (out == drv.outputs.end())
        return nullptr;
    if (auto dof = std::get_if<DerivationOutput::CAFixed>(&out->second.raw)) {
        return &dof->ca;
    }
    return nullptr;
}

namespace {
struct QueryMissingContext
{
    Store & store;

    struct State
    {
        std::unordered_set<std::string> done;
        StorePathSet & unknown, & willSubstitute, & willBuild;
        uint64_t & downloadSize;
        uint64_t & narSize;
    };

    struct DrvState
    {
        size_t left;
        bool done = false;
        StorePathSet outPaths;
        DrvState(size_t left) : left(left) { }
    };

    State state;

    explicit QueryMissingContext(
        Store & store,
        StorePathSet & willBuild_,
        StorePathSet & willSubstitute_,
        StorePathSet & unknown_,
        uint64_t & downloadSize_,
        uint64_t & narSize_
    )
        : store(store)
        , state{{}, unknown_, willSubstitute_, willBuild_, downloadSize_, narSize_}
    {
    }

    KJ_DISALLOW_COPY_AND_MOVE(QueryMissingContext);

    kj::Promise<Result<void>> queryMissing(const std::vector<DerivedPath> & targets);

    kj::Promise<Result<void>>
    enqueueDerivedPaths(DerivedPathOpaque inputDrv, const StringSet & inputNode)
    try {
        if (!inputNode.empty()) {
            TRY_AWAIT(doPath(DerivedPath::Built{std::move(inputDrv), inputNode}));
        }
        co_return result::success();
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<void>> mustBuildDrv(const StorePath & drvPath, const Derivation & drv)
    try {
        state.willBuild.insert(drvPath);

        TRY_AWAIT(asyncSpread(drv.inputDrvs, [&](const auto & input) {
            const auto & [inputDrv, inputNode] = input;
            return enqueueDerivedPaths(makeConstantStorePath(inputDrv), inputNode);
        }));
        co_return result::success();
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<void>> checkOutput(
        const StorePath & drvPath,
        ref<Derivation> drv,
        const StorePath & outPath,
        DrvState & drvState
    )
    try {
        SubstitutablePathInfos infos;
        auto * cap = getDerivationCA(*drv);
        TRY_AWAIT(store.querySubstitutablePathInfos(
            {
                {
                    outPath,
                    cap ? std::optional{*cap} : std::nullopt,
                },
            },
            infos
        ));

        if (infos.empty()) {
            drvState.done = true;
            TRY_AWAIT(mustBuildDrv(drvPath, *drv));
        } else {
            {
                if (drvState.done) {
                    co_return result::success();
                }
                assert(drvState.left);
                drvState.left--;
                drvState.outPaths.insert(outPath);
                if (!drvState.left) {
                    TRY_AWAIT(asyncSpread(drvState.outPaths, [&](auto & path) {
                        return doPath(DerivedPath::Opaque{path});
                    }));
                }
            }
        }

        co_return result::success();
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<void>> doPath(DerivedPath req)
    {
        if (!state.done.insert(req.to_string(store)).second) {
            return {result::success()};
        }

        return std::visit(
            overloaded{
                [&](DerivedPath::Built bfd) { return doPathBuilt(std::move(bfd)); },
                [&](DerivedPath::Opaque bo) { return doPathOpaque(std::move(bo)); },
            },
            std::move(req.raw())
        );
    }

    kj::Promise<Result<void>> doPathBuilt(DerivedPath::Built bfd)
    try {
        auto & drvPath = bfd.drvPath.path;

        if (!TRY_AWAIT(store.isValidPath(drvPath))) {
            // FIXME: we could try to substitute the derivation.
            state.unknown.insert(drvPath);
            co_return result::success();
        }

        StorePathSet invalid;
        for (auto & [outputName, path] : TRY_AWAIT(store.queryDerivationOutputMap(drvPath))) {
            if (bfd.outputs.contains(outputName) && !TRY_AWAIT(store.isValidPath(path))) {
                invalid.insert(path);
            }
        }
        if (invalid.empty()) {
            co_return result::success();
        }

        auto drv = make_ref<Derivation>(TRY_AWAIT(store.derivationFromPath(drvPath)));
        ParsedDerivation parsedDrv(StorePath(drvPath), *drv);

        if (settings.useSubstitutes && parsedDrv.substitutesAllowed()) {
            DrvState drvState(invalid.size());
            TRY_AWAIT(asyncSpread(invalid, [&](auto & output) {
                return checkOutput(drvPath, drv, output, drvState);
            }));
        } else {
            TRY_AWAIT(mustBuildDrv(drvPath, *drv));
        }

        co_return result::success();
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<void>> doPathOpaque(DerivedPath::Opaque bo)
    try {
        if (TRY_AWAIT(store.isValidPath(bo.path))) {
            co_return result::success();
        }

        SubstitutablePathInfos infos;
        TRY_AWAIT(store.querySubstitutablePathInfos({{bo.path, std::nullopt}}, infos));

        if (infos.empty()) {
            state.unknown.insert(bo.path);
            co_return result::success();
        }

        auto info = infos.find(bo.path);
        assert(info != infos.end());

        state.willSubstitute.insert(bo.path);
        state.downloadSize += info->second.downloadSize;
        state.narSize += info->second.narSize;

        TRY_AWAIT(asyncSpread(info->second.references, [&](auto & ref) {
            return doPath(DerivedPath::Opaque{ref});
        }));

        co_return result::success();
    } catch (...) {
        co_return result::current_exception();
    }
};
}

kj::Promise<Result<void>> QueryMissingContext::queryMissing(const std::vector<DerivedPath> & targets)
try {
    TRY_AWAIT(asyncSpread(targets, [&](auto & path) { return doPath(path); }));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<void>> Store::queryMissing(const std::vector<DerivedPath> & targets,
    StorePathSet & willBuild_, StorePathSet & willSubstitute_, StorePathSet & unknown_,
    uint64_t & downloadSize_, uint64_t & narSize_)
try {
    Activity act(*logger, lvlDebug, actUnknown, "querying info about missing paths");

    downloadSize_ = narSize_ = 0;

    TRY_AWAIT(
        QueryMissingContext{*this, willBuild_, willSubstitute_, unknown_, downloadSize_, narSize_}
            .queryMissing(targets)
    );
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<StorePaths>> Store::topoSortPaths(const StorePathSet & paths)
try {
    co_return TRY_AWAIT(topoSortAsync(paths,
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
        {[&](const StorePath & path) -> kj::Promise<Result<StorePathSet>> {
            try {
                co_return TRY_AWAIT(queryPathInfo(path))->references;
            } catch (InvalidPath &) {
                co_return StorePathSet();
            } catch (...) {
                co_return result::current_exception();
            }
        }},
        {[&](const StorePath & path, const StorePath & parent) {
            return BuildError(
                "cycle detected in the references of '%s' from '%s'",
                printStorePath(path),
                printStorePath(parent));
        }}));
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<OutputPathMap>>
resolveDerivedPath(Store & store, const DerivedPath::Built & bfd, Store * evalStore_)
try {
    auto drvPath = bfd.drvPath.path;

    auto outputs_ = TRY_AWAIT(store.queryDerivationOutputMap(drvPath, evalStore_));

    co_return std::visit(overloaded {
        [&](const OutputsSpec::All &) {
            // Keep all outputs
            return std::move(outputs_);
        },
        [&](const OutputsSpec::Names & names) {
            // Get just those mentioned by name
            std::map<std::string, StorePath> outputsOpt;
            for (auto & output : names) {
                auto * pOutputPathOpt = get(outputs_, output);
                if (!pOutputPathOpt)
                    throw Error(
                        "the derivation '%s' doesn't have an output named '%s'",
                        bfd.drvPath.to_string(store), output);
                outputsOpt.insert_or_assign(output, std::move(*pOutputPathOpt));
            }
            return outputsOpt;
        },
    }, bfd.outputs.raw);
} catch (...) {
    co_return result::current_exception();
}

}
