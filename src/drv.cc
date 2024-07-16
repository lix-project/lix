#include <lix/config.h> // IWYU pragma: keep

#include <lix/libstore/path-with-outputs.hh>
#include <lix/libstore/store-api.hh>
#include <lix/libstore/local-fs-store.hh>
#include <lix/libexpr/value-to-json.hh>
#include <lix/libstore/derivations.hh>
#include <stdint.h>
#include <lix/libstore/derived-path-map.hh>
#include <lix/libexpr/eval.hh>
#include <lix/libexpr/get-drvs.hh>
#include <lix/libexpr/nixexpr.hh>
#include <nlohmann/detail/json_ref.hpp>
#include <lix/libstore/path.hh>
#include <lix/libutil/ref.hh>
#include <lix/libexpr/value/context.hh>
#include <exception>
#include <memory>
#include <sstream>
#include <utility>
#include <vector>

#include "drv.hh"
#include "eval-args.hh"

static bool
queryIsCached(nix::Store &store,
              std::map<std::string, std::optional<std::string>> &outputs) {
    uint64_t downloadSize, narSize;
    nix::StorePathSet willBuild, willSubstitute, unknown;

    std::vector<nix::StorePathWithOutputs> paths;
    for (auto const &[key, val] : outputs) {
        if (val) {
            paths.push_back(followLinksToStorePathWithOutputs(store, *val));
        }
    }

    store.queryMissing(toDerivedPaths(paths), willBuild, willSubstitute,
                       unknown, downloadSize, narSize);
    return willBuild.empty() && unknown.empty();
}

/* The fields of a derivation that are printed in json form */
Drv::Drv(std::string &attrPath, nix::EvalState &state, nix::DrvInfo &drvInfo,
         MyArgs &args) {

    auto localStore = state.store.dynamic_pointer_cast<nix::LocalFSStore>();

    try {
        // CA derivations do not have static output paths, so we have to
        // defensively not query output paths in case we encounter one.
        for (auto &[outputName, optOutputPath] :
             drvInfo.queryOutputs(!nix::experimentalFeatureSettings.isEnabled(
                 nix::Xp::CaDerivations))) {
            if (optOutputPath) {
                outputs[outputName] =
                    localStore->printStorePath(*optOutputPath);
            } else {
                assert(nix::experimentalFeatureSettings.isEnabled(
                    nix::Xp::CaDerivations));
                outputs[outputName] = std::nullopt;
            }
        }
    } catch (const std::exception &e) {
        throw nix::EvalError(state,
            "derivation '%s' does not have valid outputs: %s",
            attrPath, e.what());
    }

    if (args.meta) {
        nlohmann::json meta_;
        for (auto &metaName : drvInfo.queryMetaNames()) {
            nix::NixStringContext context;
            std::stringstream ss;

            auto metaValue = drvInfo.queryMeta(metaName);
            // Skip non-serialisable types
            // TODO: Fix serialisation of derivations to store paths
            if (metaValue == 0) {
                continue;
            }

            nix::printValueAsJSON(state, true, *metaValue, nix::noPos, ss,
                                  context);

            meta_[metaName] = nlohmann::json::parse(ss.str());
        }
        meta = meta_;
    }
    if (args.checkCacheStatus) {
        cacheStatus = queryIsCached(*localStore, outputs)
                          ? Drv::CacheStatus::Cached
                          : Drv::CacheStatus::Uncached;
    } else {
        cacheStatus = Drv::CacheStatus::Unknown;
    }

    drvPath = localStore->printStorePath(drvInfo.requireDrvPath());

    auto drv = localStore->readDerivation(drvInfo.requireDrvPath());
    for (const auto &[inputDrvPath, inputNode] : drv.inputDrvs.map) {
        std::set<std::string> inputDrvOutputs;
        for (auto &outputName : inputNode.value) {
            inputDrvOutputs.insert(outputName);
        }
        inputDrvs[localStore->printStorePath(inputDrvPath)] = inputDrvOutputs;
    }
    name = drvInfo.queryName();
    system = drv.platform;
}

void to_json(nlohmann::json &json, const Drv &drv) {
    std::map<std::string, nlohmann::json> outputsJson;
    for (auto &[name, optPath] : drv.outputs) {
        outputsJson[name] =
            optPath ? nlohmann::json(*optPath) : nlohmann::json(nullptr);
    }

    json = nlohmann::json{{"name", drv.name},
                          {"system", drv.system},
                          {"drvPath", drv.drvPath},
                          {"outputs", outputsJson},
                          {"inputDrvs", drv.inputDrvs}};

    if (drv.meta.has_value()) {
        json["meta"] = drv.meta.value();
    }

    if (drv.cacheStatus != Drv::CacheStatus::Unknown) {
        json["isCached"] = drv.cacheStatus == Drv::CacheStatus::Cached;
    }
}
