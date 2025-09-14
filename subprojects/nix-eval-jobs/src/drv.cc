#include <lix/config.h> // IWYU pragma: keep

#include <lix/libstore/path-with-outputs.hh>
#include <lix/libstore/store-api.hh>
#include <lix/libstore/local-fs-store.hh>
#include <lix/libexpr/value-to-json.hh>
#include <lix/libstore/derivations.hh>
#include <stdint.h>
#include <lix/libexpr/eval.hh>
#include <lix/libexpr/get-drvs.hh>
#include <lix/libexpr/nixexpr.hh>
#include <lix/libstore/path.hh>
#include <lix/libutil/json.hh>
#include <lix/libutil/ref.hh>
#include <lix/libexpr/value/context.hh>
#include <exception>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

#include "drv.hh"
#include "eval-args.hh"
#include "lix/libutil/async.hh"

static bool
queryIsCached(nix::AsyncIoRoot &aio,
              nix::Store &store,
              std::map<std::string, std::optional<std::string>> &outputs) {
    uint64_t downloadSize, narSize;
    nix::StorePathSet willBuild, willSubstitute, unknown;

    std::vector<nix::StorePathWithOutputs> paths;
    for (auto const &[key, val] : outputs) {
        if (val) {
            paths.push_back(followLinksToStorePathWithOutputs(store, *val));
        }
    }

    aio.blockOn(store.queryMissing(toDerivedPaths(paths), willBuild, willSubstitute,
                       unknown, downloadSize, narSize));
    return willBuild.empty() && unknown.empty();
}

/* The fields of a derivation that are printed in json form */
Drv::Drv(std::string &attrPath, nix::EvalState &state, nix::DrvInfo &drvInfo,
         MyArgs &args, std::optional<Constituents> constituents)
    : constituents(constituents) {

    auto localStore = state.ctx.store.try_cast_shared<nix::LocalFSStore>();
    auto canReadDerivation = localStore && !nix::settings.readOnlyMode;

    try {
        for (auto &[outputName, optOutputPath] :
             drvInfo.queryOutputs(state, true)) {
            assert(optOutputPath);
            outputs[outputName] = state.ctx.store->printStorePath(*optOutputPath);
        }
    } catch (const std::exception &e) { // NOLINT(lix-foreign-exceptions)
        state.ctx.errors.make<nix::EvalError>(
            "derivation '%s' does not have valid outputs: %s",
            attrPath, e.what()
        ).debugThrow();
    }

    if (args.meta) {
        nix::JSON meta_;
        for (auto &metaName : drvInfo.queryMetaNames(state)) {
            nix::NixStringContext context;
            std::stringstream ss;

            auto metaValue = drvInfo.queryMeta(state, metaName);
            // Skip non-serialisable types
            // TODO: Fix serialisation of derivations to store paths
            if (metaValue == 0) {
                continue;
            }

            nix::printValueAsJSON(state, true, *metaValue, nix::noPos, ss,
                                  context);

            meta_[metaName] = nix::json::parse(ss.str());
        }
        meta = meta_;
    }

    // !canReadDerivation together with checkCacheStatus is rejected in main().
    if (args.checkCacheStatus) {
        cacheStatus = queryIsCached(state.aio, *localStore, outputs)
                          ? Drv::CacheStatus::Cached
                          : Drv::CacheStatus::Uncached;
    } else {
        cacheStatus = Drv::CacheStatus::Unknown;
    }

    drvPath = state.ctx.store->printStorePath(drvInfo.requireDrvPath(state));
    name = drvInfo.queryName(state);

    if (canReadDerivation) {
        auto drv = state.aio.blockOn(localStore->readDerivation(drvInfo.requireDrvPath(state)));
        for (const auto &[inputDrvPath, inputNode] : drv.inputDrvs) {
            std::set<std::string> inputDrvOutputs;
            for (auto &outputName : inputNode) {
                inputDrvOutputs.insert(outputName);
            }
            inputDrvs[localStore->printStorePath(inputDrvPath)] = inputDrvOutputs;
        }
        system = drv.platform;
    } else {
        system = drvInfo.querySystem(state);
    }
}

void to_json(nix::JSON &json, const Drv &drv) {
    std::map<std::string, nix::JSON> outputsJson;
    for (auto &[name, optPath] : drv.outputs) {
        outputsJson[name] =
            optPath ? nix::JSON(*optPath) : nix::JSON(nullptr);
    }

    json = nix::JSON{{"name", drv.name},
                          {"system", drv.system},
                          {"drvPath", drv.drvPath},
                          {"outputs", outputsJson},
                          {"inputDrvs", drv.inputDrvs}};

    if (drv.meta.has_value()) {
        json["meta"] = drv.meta.value();
    }

    if (auto constituents = drv.constituents) {
        json["constituents"] = constituents->constituents;
        json["namedConstituents"] = constituents->namedConstituents;
    }

    if (drv.cacheStatus != Drv::CacheStatus::Unknown) {
        json["isCached"] = drv.cacheStatus == Drv::CacheStatus::Cached;
    }
}

void register_gc_root(nix::Path &gcRootsDir, std::string &drvPath, const nix::ref<nix::Store> &store,
                      nix::AsyncIoRoot &aio) {
    if (!gcRootsDir.empty() && !nix::settings.readOnlyMode) {
        nix::Path root =
            gcRootsDir + "/" +
            std::string(nix::baseNameOf(drvPath));
        if (!nix::pathExists(root)) {
            auto localStore = store.try_cast_shared<nix::LocalFSStore>();
            if (localStore) {
                auto storePath =
                    localStore->parseStorePath(drvPath);
                aio.blockOn(localStore->addPermRoot(storePath, root));
            }
        }
    }
}
