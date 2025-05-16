#include "lix/libstore/realisation.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/closure.hh"
#include "lix/libutil/json.hh"
#include "lix/libutil/result.hh"

namespace nix {

MakeError(InvalidDerivationOutputId, Error);

DrvOutput DrvOutput::parse(const std::string &strRep) {
    size_t n = strRep.find("!");
    if (n == strRep.npos)
        throw InvalidDerivationOutputId("Invalid derivation output id %s", strRep);

    return DrvOutput{
        .drvHash = Hash::parseAnyPrefixed(strRep.substr(0, n)),
        .outputName = strRep.substr(n+1),
    };
}

std::string DrvOutput::to_string() const {
    return strHash() + "!" + outputName;
}

JSON Realisation::toJSON() const {
    auto jsonDependentRealisations = JSON::object();
    for (auto & [depId, depOutPath] : dependentRealisations)
        jsonDependentRealisations.emplace(depId.to_string(), depOutPath.to_string());
    return JSON{
        {"id", id.to_string()},
        {"outPath", outPath.to_string()},
        {"signatures", signatures},
        {"dependentRealisations", jsonDependentRealisations},
    };
}

Realisation Realisation::fromJSON(
    const JSON& json,
    const std::string& whence) {
    auto getOptionalField = [&](std::string fieldName) -> std::optional<std::string> {
        auto fieldIterator = json.find(fieldName);
        if (fieldIterator == json.end())
            return std::nullopt;
        return {*fieldIterator};
    };
    auto getField = [&](std::string fieldName) -> std::string {
        if (auto field = getOptionalField(fieldName))
            return *field;
        else
            throw Error(
                "Drv output info file '%1%' is corrupt, missing field %2%",
                whence, fieldName);
    };

    StringSet signatures;
    if (auto signaturesIterator = json.find("signatures"); signaturesIterator != json.end())
        signatures.insert(signaturesIterator->begin(), signaturesIterator->end());

    std::map <DrvOutput, StorePath> dependentRealisations;
    if (auto jsonDependencies = json.find("dependentRealisations"); jsonDependencies != json.end())
        for (auto & [jsonDepId, jsonDepOutPath] : jsonDependencies->get<std::map<std::string, std::string>>())
            dependentRealisations.insert({DrvOutput::parse(jsonDepId), StorePath(jsonDepOutPath)});

    return Realisation{
        .id = DrvOutput::parse(getField("id")),
        .outPath = StorePath(getField("outPath")),
        .signatures = signatures,
        .dependentRealisations = dependentRealisations,
    };
}


SingleDrvOutputs filterDrvOutputs(const OutputsSpec& wanted, SingleDrvOutputs&& outputs)
{
    SingleDrvOutputs ret = std::move(outputs);
    for (auto it = ret.begin(); it != ret.end(); ) {
        if (!wanted.contains(it->first))
            it = ret.erase(it);
        else
            ++it;
    }
    return ret;
}

StorePath RealisedPath::path() const {
    return std::visit([](auto && arg) { return arg.getPath(); }, raw);
}

kj::Promise<Result<void>> RealisedPath::closure(
    Store& store,
    const RealisedPath::Set& startPaths,
    RealisedPath::Set& ret)
try {
    // FIXME: This only builds the store-path closure, not the real realisation
    // closure
    StorePathSet initialStorePaths, pathsClosure;
    for (auto& path : startPaths)
        initialStorePaths.insert(path.path());
    TRY_AWAIT(store.computeFSClosure(initialStorePaths, pathsClosure));
    ret.insert(startPaths.begin(), startPaths.end());
    ret.insert(pathsClosure.begin(), pathsClosure.end());
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

} // namespace nix
