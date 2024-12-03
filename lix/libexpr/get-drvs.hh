#pragma once
///@file

#include "lix/libexpr/eval.hh"
#include "lix/libstore/path.hh"

#include <string>
#include <map>


namespace nix {


struct DrvInfo
{
public:
    typedef std::map<std::string, std::optional<StorePath>> Outputs;

private:
    EvalState * state;

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

    Bindings * getMeta();

    bool checkMeta(Value & v);

    void fillOutputs(bool withPaths = true);

public:
    /**
     * path towards the derivation
     */
    std::string attrPath;

    DrvInfo(EvalState & state, std::string attrPath, Bindings * attrs);
    DrvInfo(EvalState & state, ref<Store> store, const std::string & drvPathWithOutputs);

    std::string queryName();
    std::string querySystem();
    std::optional<StorePath> queryDrvPath();
    StorePath requireDrvPath();
    StorePath queryOutPath();
    std::string queryOutputName();
    /**
     * Return the unordered map of output names to (optional) output paths.
     * The "outputs to install" are determined by `meta.outputsToInstall`.
     */
    Outputs queryOutputs(bool withPaths = true, bool onlyOutputsToInstall = false);

    StringSet queryMetaNames();
    Value * queryMeta(const std::string & name);
    std::string queryMetaString(const std::string & name);
    NixInt queryMetaInt(const std::string & name, NixInt def);
    bool queryMetaBool(const std::string & name, bool def);
    void setMeta(const std::string & name, Value * v);

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
