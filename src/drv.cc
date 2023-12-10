#include "drv.hh"
#include <nix/config.h>
#include <nix/path-with-outputs.hh>
#include <nix/store-api.hh>
#include <nix/local-fs-store.hh>
#include <nix/value-to-json.hh>
#include <nix/derivations.hh>


static bool queryIsCached(nix::Store &store,
                   std::map<std::string, std::string> &outputs) {
    uint64_t downloadSize, narSize;
    nix::StorePathSet willBuild, willSubstitute, unknown;

    std::vector<nix::StorePathWithOutputs> paths;
    for (auto const &[key, val] : outputs) {
        paths.push_back(followLinksToStorePathWithOutputs(store, val));
    }

    store.queryMissing(toDerivedPaths(paths), willBuild, willSubstitute,
                       unknown, downloadSize, narSize);
    return willBuild.empty() && unknown.empty();
}

/* The fields of a derivation that are printed in json form */
Drv::Drv(std::string &attrPath, nix::EvalState &state, nix::DrvInfo &drvInfo, MyArgs &args) {

    auto localStore = state.store.dynamic_pointer_cast<nix::LocalFSStore>();

    try {
        for (auto out : drvInfo.queryOutputs(true)) {
            if (out.second)
                outputs[out.first] = localStore->printStorePath(*out.second);
        }
    } catch (const std::exception &e) {
        throw nix::EvalError("derivation '%s' does not have valid outputs: %s",
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
        cacheStatus = queryIsCached(*localStore, outputs) ? Drv::CacheStatus::Cached
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
    json = nlohmann::json{{"name", drv.name},
                          {"system", drv.system},
                          {"drvPath", drv.drvPath},
                          {"outputs", drv.outputs},
                          {"inputDrvs", drv.inputDrvs}};

    if (drv.meta.has_value()) {
        json["meta"] = drv.meta.value();
    }

    if (drv.cacheStatus != Drv::CacheStatus::Unknown) {
        json["isCached"] = drv.cacheStatus == Drv::CacheStatus::Cached;
    }
}
