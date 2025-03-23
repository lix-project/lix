#include "lix/libcmd/built-path.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/json.hh"
#include "lix/libutil/result.hh"

#include <kj/async.h>

#include <optional>

namespace nix {

#define CMP_ONE(CHILD_TYPE, MY_TYPE, FIELD, COMPARATOR) \
    bool MY_TYPE ::operator COMPARATOR (const MY_TYPE & other) const \
    { \
        const MY_TYPE* me = this; \
        auto fields1 = std::tie(*me->drvPath, me->FIELD); \
        me = &other; \
        auto fields2 = std::tie(*me->drvPath, me->FIELD); \
        return fields1 COMPARATOR fields2; \
    }
#define CMP(CHILD_TYPE, MY_TYPE, FIELD) \
    CMP_ONE(CHILD_TYPE, MY_TYPE, FIELD, ==) \
    CMP_ONE(CHILD_TYPE, MY_TYPE, FIELD, !=) \
    CMP_ONE(CHILD_TYPE, MY_TYPE, FIELD, <)

#define FIELD_TYPE std::pair<std::string, StorePath>
CMP(SingleBuiltPath, SingleBuiltPathBuilt, output)
#undef FIELD_TYPE

#define FIELD_TYPE std::map<std::string, StorePath>
CMP(SingleBuiltPath, BuiltPathBuilt, outputs)
#undef FIELD_TYPE

#undef CMP
#undef CMP_ONE

StorePath SingleBuiltPath::outPath() const
{
    return std::visit(
        overloaded{
            [](const SingleBuiltPath::Opaque & p) { return p.path; },
            [](const SingleBuiltPath::Built & b) { return b.output.second; },
        }, raw()
    );
}

StorePathSet BuiltPath::outPaths() const
{
    return std::visit(
        overloaded{
            [](const BuiltPath::Opaque & p) { return StorePathSet{p.path}; },
            [](const BuiltPath::Built & b) {
                StorePathSet res;
                for (auto & [_, path] : b.outputs)
                    res.insert(path);
                return res;
            },
        }, raw()
    );
}

SingleDerivedPath::Built SingleBuiltPath::Built::discardOutputPath() const
{
    return SingleDerivedPath::Built {
        .drvPath = make_ref<SingleDerivedPath>(drvPath->discardOutputPath()),
        .output = output.first,
    };
}

SingleDerivedPath SingleBuiltPath::discardOutputPath() const
{
    return std::visit(
        overloaded{
            [](const SingleBuiltPath::Opaque & p) -> SingleDerivedPath {
                return p;
            },
            [](const SingleBuiltPath::Built & b) -> SingleDerivedPath {
                return b.discardOutputPath();
            },
        }, raw()
    );
}

kj::Promise<Result<nlohmann::json>> BuiltPath::Built::toJSON(const Store & store) const
try {
    nlohmann::json res;
    res["drvPath"] = TRY_AWAIT(drvPath->toJSON(store));
    for (const auto & [outputName, outputPath] : outputs) {
        res["outputs"][outputName] = store.printStorePath(outputPath);
    }
    co_return res;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<nlohmann::json>> SingleBuiltPath::Built::toJSON(const Store & store) const
try {
    nlohmann::json res;
    res["drvPath"] = TRY_AWAIT(drvPath->toJSON(store));
    auto & [outputName, outputPath] = output;
    res["output"] = outputName;
    res["outputPath"] = store.printStorePath(outputPath);
    co_return res;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<nlohmann::json>> SingleBuiltPath::toJSON(const Store & store) const
try {
    co_return TRY_AWAIT(std::visit([&](const auto & buildable) {
        return buildable.toJSON(store);
    }, raw()));
} catch (...) {
    co_return result::current_exception();
}


kj::Promise<Result<nlohmann::json>> BuiltPath::toJSON(const Store & store) const
try {
    co_return TRY_AWAIT(std::visit([&](const auto & buildable) {
        return buildable.toJSON(store);
    }, raw()));
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<RealisedPath::Set>> BuiltPath::toRealisedPaths(Store & store) const
try {
    RealisedPath::Set res;
    auto handlers = overloaded{
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
        [&](const BuiltPath::Opaque & p) -> kj::Promise<Result<void>> {
            try {
                res.insert(p.path);
                return {result::success()};
            } catch (...) {
                return {result::current_exception()};
            }
        },
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
        [&](const BuiltPath::Built & p) -> kj::Promise<Result<void>> {
            try {
                auto drvHashes = TRY_AWAIT(
                    staticOutputHashes(store, TRY_AWAIT(store.readDerivation(p.drvPath->outPath())))
                );
                for (auto& [outputName, outputPath] : p.outputs) {
                    if (experimentalFeatureSettings.isEnabled(
                                Xp::CaDerivations)) {
                        auto drvOutput = get(drvHashes, outputName);
                        if (!drvOutput)
                            throw Error(
                                "the derivation '%s' has unrealised output '%s' (derived-path.cc/toRealisedPaths)",
                                store.printStorePath(p.drvPath->outPath()), outputName);
                        auto thisRealisation = TRY_AWAIT(store.queryRealisation(
                            DrvOutput{*drvOutput, outputName}));
                        assert(thisRealisation);  // We’ve built it, so we must
                                                  // have the realisation
                        res.insert(*thisRealisation);
                    } else {
                        res.insert(outputPath);
                    }
                }
                co_return result::success();
            } catch (...) {
                co_return result::current_exception();
            }
        },
    };
    TRY_AWAIT(std::visit(handlers, raw()));
    co_return res;
} catch (...) {
    co_return result::current_exception();
}

}
