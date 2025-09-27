#pragma once
///@file

#include "lix/libexpr/eval.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libstore/path.hh"

#include <string>
#include <map>


namespace nix {


struct DrvInfo
{
public:
    typedef std::map<std::string, std::optional<StorePath>> Outputs;

private:
    std::string name;
    std::string system;
    std::optional<std::optional<StorePath>> drvPath;
    std::optional<StorePath> outPath;
    std::string outputName;
    Outputs outputs;

    /**
     * Set if we get an AssertionError
     */
    bool failed = false;

    Bindings * attrs = nullptr, * meta = nullptr;

    Bindings * getMeta(EvalState & state);

    bool checkMeta(EvalState & state, Value & v);

    void fillOutputs(EvalState & state, bool withPaths = true);

    DrvInfo(
        ref<Store> store,
        const std::string & drvPathWithOutputs,
        Derivation drv,
        const StorePath & drvPath,
        const std::set<std::string> & selectedOutputs
    );

public:
    /**
     * path towards the derivation
     */
    std::string attrPath;

    DrvInfo(std::string attrPath, Bindings * attrs);
    static kj::Promise<Result<DrvInfo>>
    create(ref<Store> store, const std::string & drvPathWithOutputs);

    std::string queryName(EvalState & state);
    std::string querySystem(EvalState & state);
    std::optional<StorePath> queryDrvPath(EvalState & state);
    StorePath requireDrvPath(EvalState & state);
    StorePath queryOutPath(EvalState & state);
    std::string queryOutputName(EvalState & state);
    /**
     * Return the unordered map of output names to (optional) output paths.
     * The "outputs to install" are determined by `meta.outputsToInstall`.
     */
    Outputs queryOutputs(EvalState & state, bool withPaths = true, bool onlyOutputsToInstall = false);

    StringSet queryMetaNames(EvalState & state);
    Value * queryMeta(EvalState & state, const std::string & name);
    std::string queryMetaString(EvalState & state, const std::string & name);
    NixInt queryMetaInt(EvalState & state, const std::string & name, NixInt def);
    bool queryMetaBool(EvalState & state, const std::string & name, bool def);
    void setMeta(EvalState & state, const std::string & name, Value & v);

    /*
    MetaInfo queryMetaInfo(EvalState & state) const;
    MetaValue queryMetaInfo(EvalState & state, const string & name) const;
    */

    void setName(const std::string & s) { name = s; }
    void setDrvPath(StorePath path) { drvPath = {{std::move(path)}}; }
    void setOutPath(StorePath path) { outPath = {{std::move(path)}}; }

    void setFailed() { failed = true; };
    bool hasFailed() { return failed; };
};

using DrvInfos = GcList<DrvInfo>;

/**
 * If value `v` denotes a derivation, return a DrvInfo object
 * describing it. Otherwise return nothing.
 */
std::optional<DrvInfo> getDerivation(EvalState & state,
    Value & v, bool ignoreAssertionFailures);

void getDerivations(EvalState & state, Value & v, const std::string & pathPrefix,
    Bindings & autoArgs, DrvInfos & drvs,
    bool ignoreAssertionFailures);


}
