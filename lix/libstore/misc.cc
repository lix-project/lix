#include "lix/libstore/derivations.hh"
#include "lix/libstore/parsed-derivations.hh"
#include "lix/libstore/globals.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/thread-pool.hh"
#include "lix/libutil/topo-sort.hh"
#include "lix/libutil/closure.hh"
#include "lix/libstore/filetransfer.hh"
#include "lix/libutil/strings.hh"
#include <kj/common.h>

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
                    for (auto& [_, maybeOutPath] : TRY_AWAIT(queryPartialDerivationOutputMap(path)))
                        if (maybeOutPath && isValidPath(*maybeOutPath))
                            res.insert(*maybeOutPath);
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
                    for (auto& [_, maybeOutPath] : TRY_AWAIT(queryPartialDerivationOutputMap(path)))
                        if (maybeOutPath && isValidPath(*maybeOutPath))
                            res.insert(*maybeOutPath);

                if (includeDerivers && info->deriver && isValidPath(*info->deriver))
                    res.insert(*info->deriver);
                co_return res;
            } catch (...) {
                co_return result::current_exception();
            }
        };
    }

    paths_.merge(TRY_AWAIT(computeClosureAsync<StorePath>(
        startPaths,
        [&](const StorePath& path) -> kj::Promise<Result<std::set<StorePath>>> {
            return queryDeps(path, queryPathInfo(path));
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

    Sync<State> state_;

    // FIXME: make async.
    ThreadPool pool{"queryMissing pool", fileTransferSettings.httpConnections};

    explicit QueryMissingContext(
        Store & store,
        StorePathSet & willBuild_,
        StorePathSet & willSubstitute_,
        StorePathSet & unknown_,
        uint64_t & downloadSize_,
        uint64_t & narSize_
    )
        : store(store)
        , state_{{{}, unknown_, willSubstitute_, willBuild_, downloadSize_, narSize_}}
    {
    }

    KJ_DISALLOW_COPY_AND_MOVE(QueryMissingContext);

    kj::Promise<Result<void>> queryMissing(const std::vector<DerivedPath> & targets);

    void enqueueDerivedPaths(ref<SingleDerivedPath> inputDrv, const DerivedPathMap<StringSet>::ChildNode & inputNode)
    {
        if (!inputNode.value.empty()) {
            pool.enqueueWithAio([this, path{DerivedPath::Built{inputDrv, inputNode.value}}](
                                    AsyncIoRoot & aio
                                ) { doPath(aio, path); });
        }
        for (const auto & [outputName, childNode] : inputNode.childMap)
            enqueueDerivedPaths(
                make_ref<SingleDerivedPath>(SingleDerivedPath::Built { inputDrv, outputName }),
                childNode);
    }

    void mustBuildDrv(const StorePath & drvPath, const Derivation & drv)
    {
        {
            auto state(state_.lock());
            state->willBuild.insert(drvPath);
        }

        for (const auto & [inputDrv, inputNode] : drv.inputDrvs.map) {
            enqueueDerivedPaths(makeConstantStorePathRef(inputDrv), inputNode);
        }
    }

    void checkOutput(
        AsyncIoRoot & aio,
        const StorePath & drvPath,
        ref<Derivation> drv,
        const StorePath & outPath,
        ref<Sync<DrvState>> drvState_
    )
    {
        if (drvState_->lock()->done) return;

        SubstitutablePathInfos infos;
        auto * cap = getDerivationCA(*drv);
        aio.blockOn(store.querySubstitutablePathInfos({
            {
                outPath,
                cap ? std::optional { *cap } : std::nullopt,
            },
        }, infos));

        if (infos.empty()) {
            drvState_->lock()->done = true;
            mustBuildDrv(drvPath, *drv);
        } else {
            {
                auto drvState(drvState_->lock());
                if (drvState->done) return;
                assert(drvState->left);
                drvState->left--;
                drvState->outPaths.insert(outPath);
                if (!drvState->left) {
                    for (auto & path : drvState->outPaths) {
                        pool.enqueueWithAio([this,
                                             path{DerivedPath::Opaque{path}}](AsyncIoRoot & aio) {
                            doPath(aio, path);
                        });
                    }
                }
            }
        }
    }

    void doPath(AsyncIoRoot & aio, const DerivedPath & req)
    {
        {
            auto state(state_.lock());
            if (!state->done.insert(req.to_string(store)).second) return;
        }

        std::visit(
            overloaded{
                [&](const DerivedPath::Built & bfd) { doPathBuilt(aio, bfd); },
                [&](const DerivedPath::Opaque & bo) { doPathOpaque(aio, bo); },
            },
            req.raw()
        );
    }

    void doPathBuilt(AsyncIoRoot & aio, const DerivedPath::Built & bfd)
    {
        auto drvPathP = std::get_if<DerivedPath::Opaque>(&*bfd.drvPath);
        if (!drvPathP) {
            // TODO make work in this case.
            warn("Ignoring dynamic derivation %s while querying missing paths; not yet implemented", bfd.drvPath->to_string(store));
            return;
        }
        auto & drvPath = drvPathP->path;

        if (!store.isValidPath(drvPath)) {
            // FIXME: we could try to substitute the derivation.
            auto state(state_.lock());
            state->unknown.insert(drvPath);
            return;
        }

        StorePathSet invalid;
        /* true for regular derivations, and CA derivations for which we
            have a trust mapping for all wanted outputs. */
        auto knownOutputPaths = true;
        for (auto & [outputName, pathOpt] :
             aio.blockOn(store.queryPartialDerivationOutputMap(drvPath)))
        {
            if (!pathOpt) {
                knownOutputPaths = false;
                break;
            }
            if (bfd.outputs.contains(outputName) && !store.isValidPath(*pathOpt))
                invalid.insert(*pathOpt);
        }
        if (knownOutputPaths && invalid.empty()) return;

        auto drv = make_ref<Derivation>(aio.blockOn(store.derivationFromPath(drvPath)));
        ParsedDerivation parsedDrv(StorePath(drvPath), *drv);

        if (!knownOutputPaths && settings.useSubstitutes && parsedDrv.substitutesAllowed()) {
            experimentalFeatureSettings.require(Xp::CaDerivations);

            // If there are unknown output paths, attempt to find if the
            // paths are known to substituters through a realisation.
            auto outputHashes = staticOutputHashes(store, *drv);
            knownOutputPaths = true;

            for (auto [outputName, hash] : outputHashes) {
                if (!bfd.outputs.contains(outputName))
                    continue;

                bool found = false;
                for (auto &sub : aio.blockOn(getDefaultSubstituters())) {
                    auto realisation = aio.blockOn(sub->queryRealisation({hash, outputName}));
                    if (!realisation)
                        continue;
                    found = true;
                    if (!store.isValidPath(realisation->outPath))
                        invalid.insert(realisation->outPath);
                    break;
                }
                if (!found) {
                    // Some paths did not have a realisation, this must be built.
                    knownOutputPaths = false;
                    break;
                }
            }
        }

        if (knownOutputPaths && settings.useSubstitutes && parsedDrv.substitutesAllowed()) {
            auto drvState = make_ref<Sync<DrvState>>(DrvState(invalid.size()));
            for (auto & output : invalid) {
                pool.enqueueWithAio([=, this](AsyncIoRoot & aio) {
                    checkOutput(aio, drvPath, drv, output, drvState);
                });
            }
        } else {
            mustBuildDrv(drvPath, *drv);
        }
    }

    void doPathOpaque(AsyncIoRoot & aio, const DerivedPath::Opaque & bo)
    {
        if (store.isValidPath(bo.path)) return;

        SubstitutablePathInfos infos;
        aio.blockOn(store.querySubstitutablePathInfos({{bo.path, std::nullopt}}, infos));

        if (infos.empty()) {
            auto state(state_.lock());
            state->unknown.insert(bo.path);
            return;
        }

        auto info = infos.find(bo.path);
        assert(info != infos.end());

        {
            auto state(state_.lock());
            state->willSubstitute.insert(bo.path);
            state->downloadSize += info->second.downloadSize;
            state->narSize += info->second.narSize;
        }

        for (auto & ref : info->second.references) {
            pool.enqueueWithAio([this, path{DerivedPath::Opaque{ref}}](AsyncIoRoot & aio) {
                doPath(aio, path);
            });
        }
    }
};
}

kj::Promise<Result<void>> QueryMissingContext::queryMissing(const std::vector<DerivedPath> & targets)
try {
    for (auto & path : targets) {
        pool.enqueueWithAio([=, this](AsyncIoRoot & aio) { doPath(aio, path); });
    }

    TRY_AWAIT(pool.processAsync());
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
    co_return topoSort(paths,
        {[&](const StorePath & path) {
            try {
                return queryPathInfo(path)->references;
            } catch (InvalidPath &) {
                return StorePathSet();
            }
        }},
        {[&](const StorePath & path, const StorePath & parent) {
            return BuildError(
                "cycle detected in the references of '%s' from '%s'",
                printStorePath(path),
                printStorePath(parent));
        }});
} catch (...) {
    co_return result::current_exception();
}

static kj::Promise<Result<std::map<DrvOutput, StorePath>>> drvOutputReferences(
    const std::set<Realisation> & inputRealisations,
    const StorePathSet & pathReferences)
try {
    std::map<DrvOutput, StorePath> res;

    for (const auto & input : inputRealisations) {
        if (pathReferences.count(input.outPath)) {
            res.insert({input.id, input.outPath});
        }
    }

    co_return res;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::map<DrvOutput, StorePath>>> drvOutputReferences(
    Store & store,
    const Derivation & drv,
    const StorePath & outputPath,
    Store * evalStore_)
try {
    auto & evalStore = evalStore_ ? *evalStore_ : store;

    std::set<Realisation> inputRealisations;

    std::function<
        kj::Promise<Result<void>>(const StorePath &, const DerivedPathMap<StringSet>::ChildNode &)>
        accumRealisations;

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
    accumRealisations = [&](const StorePath & inputDrv,
                            const DerivedPathMap<StringSet>::ChildNode & inputNode
                        ) -> kj::Promise<Result<void>> {
        try {
            if (!inputNode.value.empty()) {
                auto outputHashes =
                    staticOutputHashes(evalStore, evalStore.readDerivation(inputDrv));
                for (const auto & outputName : inputNode.value) {
                    auto outputHash = get(outputHashes, outputName);
                    if (!outputHash)
                        throw Error(
                            "output '%s' of derivation '%s' isn't realised", outputName,
                            store.printStorePath(inputDrv));
                    auto thisRealisation = TRY_AWAIT(store.queryRealisation(
                        DrvOutput{*outputHash, outputName}));
                    if (!thisRealisation)
                        throw Error(
                            "output '%s' of derivation '%s' isn’t built", outputName,
                            store.printStorePath(inputDrv));
                    inputRealisations.insert(*thisRealisation);
                }
            }
            if (!inputNode.value.empty()) {
                auto d = makeConstantStorePathRef(inputDrv);
                for (const auto & [outputName, childNode] : inputNode.childMap) {
                    SingleDerivedPath next = SingleDerivedPath::Built { d, outputName };
                    TRY_AWAIT(accumRealisations(
                        // TODO deep resolutions for dynamic derivations, issue #8947, would go here.
                        TRY_AWAIT(resolveDerivedPath(store, next, evalStore_)),
                        childNode));
                }
            }
            co_return result::success();
        } catch (...) {
            co_return result::current_exception();
        }
    };

    for (const auto & [inputDrv, inputNode] : drv.inputDrvs.map)
        TRY_AWAIT(accumRealisations(inputDrv, inputNode));

    auto info = store.queryPathInfo(outputPath);

    co_return TRY_AWAIT(drvOutputReferences(
        TRY_AWAIT(Realisation::closure(store, inputRealisations)), info->references
    ));
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<OutputPathMap>>
resolveDerivedPath(Store & store, const DerivedPath::Built & bfd, Store * evalStore_)
try {
    auto drvPath = TRY_AWAIT(resolveDerivedPath(store, *bfd.drvPath, evalStore_));

    auto outputsOpt_ = TRY_AWAIT(store.queryPartialDerivationOutputMap(drvPath, evalStore_));

    auto outputsOpt = std::visit(overloaded {
        [&](const OutputsSpec::All &) {
            // Keep all outputs
            return std::move(outputsOpt_);
        },
        [&](const OutputsSpec::Names & names) {
            // Get just those mentioned by name
            std::map<std::string, std::optional<StorePath>> outputsOpt;
            for (auto & output : names) {
                auto * pOutputPathOpt = get(outputsOpt_, output);
                if (!pOutputPathOpt)
                    throw Error(
                        "the derivation '%s' doesn't have an output named '%s'",
                        bfd.drvPath->to_string(store), output);
                outputsOpt.insert_or_assign(output, std::move(*pOutputPathOpt));
            }
            return outputsOpt;
        },
    }, bfd.outputs.raw);

    OutputPathMap outputs;
    for (auto & [outputName, outputPathOpt] : outputsOpt) {
        if (!outputPathOpt)
            throw MissingRealisation(bfd.drvPath->to_string(store), outputName);
        auto & outputPath = *outputPathOpt;
        outputs.insert_or_assign(outputName, outputPath);
    }
    co_return outputs;
} catch (...) {
    co_return result::current_exception();
}


kj ::Promise<Result<StorePath>>
resolveDerivedPath(Store & store, const SingleDerivedPath & req, Store * evalStore_)
try {
    auto & evalStore = evalStore_ ? *evalStore_ : store;

    auto handlers = overloaded {
        [&](const SingleDerivedPath::Opaque & bo) -> kj::Promise<Result<StorePath>> {
            return {bo.path};
        },
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
        [&](const SingleDerivedPath::Built & bfd) -> kj::Promise<Result<StorePath>> {
            try {
                auto drvPath = TRY_AWAIT(resolveDerivedPath(store, *bfd.drvPath, evalStore_));
                auto outputPaths =
                    TRY_AWAIT(evalStore.queryPartialDerivationOutputMap(drvPath, evalStore_));
                if (outputPaths.count(bfd.output) == 0)
                    throw Error("derivation '%s' does not have an output named '%s'",
                        store.printStorePath(drvPath), bfd.output);
                auto & optPath = outputPaths.at(bfd.output);
                if (!optPath)
                    throw MissingRealisation(bfd.drvPath->to_string(store), bfd.output);
                co_return *optPath;
            } catch (...) {
                co_return result::current_exception();
            }
        },
    };
    co_return TRY_AWAIT(std::visit(handlers, req.raw()));
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<OutputPathMap>>
resolveDerivedPath(Store & store, const DerivedPath::Built & bfd)
try {
    auto drvPath = TRY_AWAIT(resolveDerivedPath(store, *bfd.drvPath));
    auto outputMap = TRY_AWAIT(store.queryDerivationOutputMap(drvPath));
    auto outputsLeft = std::visit(overloaded {
        [&](const OutputsSpec::All &) {
            return StringSet {};
        },
        [&](const OutputsSpec::Names & names) {
            return static_cast<StringSet>(names);
        },
    }, bfd.outputs.raw);
    for (auto iter = outputMap.begin(); iter != outputMap.end();) {
        auto & outputName = iter->first;
        if (bfd.outputs.contains(outputName)) {
            outputsLeft.erase(outputName);
            ++iter;
        } else {
            iter = outputMap.erase(iter);
        }
    }
    if (!outputsLeft.empty())
        throw Error("derivation '%s' does not have an outputs %s",
            store.printStorePath(drvPath),
            concatStringsSep(", ", quoteStrings(std::get<OutputsSpec::Names>(bfd.outputs.raw))));
    co_return outputMap;
} catch (...) {
    co_return result::current_exception();
}

}
