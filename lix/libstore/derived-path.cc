#include "lix/libutil/json.hh"
#include "lix/libstore/derived-path.hh"
#include "lix/libstore/store-api.hh"

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

CMP(SingleDerivedPath, SingleDerivedPathBuilt, output)

CMP(SingleDerivedPath, DerivedPathBuilt, outputs)

#undef CMP
#undef CMP_ONE

kj::Promise<Result<JSON>> DerivedPath::Opaque::toJSON(const Store & store) const
try {
    return {store.printStorePath(path)};
} catch (...) {
    return {result::current_exception()};
}

kj::Promise<Result<JSON>> SingleDerivedPath::Built::toJSON(Store & store) const
try {
    JSON res;
    res["drvPath"] = TRY_AWAIT(drvPath->toJSON(store));
    // Fallback for the input-addressed derivation case: We expect to always be
    // able to print the output paths, so let’s do it
    // FIXME try-resolve on drvPath
    const auto outputMap = TRY_AWAIT(
        store.queryPartialDerivationOutputMap(TRY_AWAIT(resolveDerivedPath(store, *drvPath)))
    );
    res["output"] = output;
    auto outputPathIter = outputMap.find(output);
    if (outputPathIter == outputMap.end())
        res["outputPath"] = nullptr;
    else if (std::optional p = outputPathIter->second)
        res["outputPath"] = store.printStorePath(*p);
    else
        res["outputPath"] = nullptr;
    co_return res;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<JSON>> DerivedPath::Built::toJSON(Store & store) const
try {
    JSON res;
    res["drvPath"] = TRY_AWAIT(drvPath->toJSON(store));
    // Fallback for the input-addressed derivation case: We expect to always be
    // able to print the output paths, so let’s do it
    // FIXME try-resolve on drvPath
    const auto outputMap = TRY_AWAIT(
        store.queryPartialDerivationOutputMap(TRY_AWAIT(resolveDerivedPath(store, *drvPath)))
    );
    for (const auto & [output, outputPathOpt] : outputMap) {
        if (!outputs.contains(output)) continue;
        if (outputPathOpt)
            res["outputs"][output] = store.printStorePath(*outputPathOpt);
        else
            res["outputs"][output] = nullptr;
    }
    co_return res;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<JSON>> SingleDerivedPath::toJSON(Store & store) const
try {
    co_return TRY_AWAIT(std::visit([&](const auto & buildable) {
        return buildable.toJSON(store);
    }, raw()));
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<JSON>> DerivedPath::toJSON(Store & store) const
try {
    co_return TRY_AWAIT(std::visit([&](const auto & buildable) {
        return buildable.toJSON(store);
    }, raw()));
} catch (...) {
    co_return result::current_exception();
}

std::string DerivedPath::Opaque::to_string(const Store & store) const
{
    return store.printStorePath(path);
}

std::string SingleDerivedPath::Built::to_string(const Store & store) const
{
    return drvPath->to_string(store) + "^" + output;
}

std::string SingleDerivedPath::Built::to_string_legacy(const Store & store) const
{
    return drvPath->to_string(store) + "!" + output;
}

std::string DerivedPath::Built::to_string(const Store & store) const
{
    return drvPath->to_string(store)
        + '^'
        + outputs.to_string();
}

std::string DerivedPath::Built::to_string_legacy(const Store & store) const
{
    return drvPath->to_string_legacy(store)
        + "!"
        + outputs.to_string();
}

std::string SingleDerivedPath::to_string(const Store & store) const
{
    return std::visit(
        [&](const auto & req) { return req.to_string(store); },
        raw());
}

std::string DerivedPath::to_string(const Store & store) const
{
    return std::visit(
        [&](const auto & req) { return req.to_string(store); },
        raw());
}

std::string SingleDerivedPath::to_string_legacy(const Store & store) const
{
    return std::visit(overloaded {
        [&](const SingleDerivedPath::Built & req) { return req.to_string_legacy(store); },
        [&](const SingleDerivedPath::Opaque & req) { return req.to_string(store); },
    }, this->raw());
}

std::string DerivedPath::to_string_legacy(const Store & store) const
{
    return std::visit(overloaded {
        [&](const DerivedPath::Built & req) { return req.to_string_legacy(store); },
        [&](const DerivedPath::Opaque & req) { return req.to_string(store); },
    }, this->raw());
}


DerivedPath::Opaque DerivedPath::Opaque::parse(const Store & store, std::string_view s)
{
    return {store.parseStorePath(s)};
}

void drvRequireExperiment(
    const SingleDerivedPath & drv,
    const ExperimentalFeatureSettings & xpSettings)
{
    std::visit(overloaded {
        [&](const SingleDerivedPath::Opaque &) {
            // plain drv path; no experimental features required.
        },
        [&](const SingleDerivedPath::Built &) {
            xpSettings.require(Xp::DynamicDerivations);
        },
    }, drv.raw());
}

SingleDerivedPath::Built SingleDerivedPath::Built::parse(
    const Store & store, ref<SingleDerivedPath> drv,
    OutputNameView output,
    const ExperimentalFeatureSettings & xpSettings)
{
    drvRequireExperiment(*drv, xpSettings);
    return {
        .drvPath = drv,
        .output = std::string { output },
    };
}

DerivedPath::Built DerivedPath::Built::parse(
    const Store & store, ref<SingleDerivedPath> drv,
    OutputNameView outputsS,
    const ExperimentalFeatureSettings & xpSettings)
{
    drvRequireExperiment(*drv, xpSettings);
    return {
        .drvPath = drv,
        .outputs = OutputsSpec::parse(outputsS),
    };
}

template <typename DerivedPathT>
static DerivedPathT parseDerivedPath(
    const Store & store, std::string_view s, std::string_view separator,
    const ExperimentalFeatureSettings & xpSettings)
{
    size_t n = s.rfind(separator);
    if (n == s.npos) {
        return DerivedPathT::Opaque::parse(store, s);
    } else {
        auto path = DerivedPathT::Built::parse(store,
            make_ref<SingleDerivedPath>(DerivedPathT::Opaque::parse(
                store,
                s.substr(0, n))),
            s.substr(n + 1),
            xpSettings);

        const auto& basePath = path.getBaseStorePath();
        if (!basePath.isDerivation()) {
            throw InvalidPath("cannot use output selection ('%s') on non-derivation store path '%s'",
                              separator, basePath.to_string());
        }

        return path;
    }
}

SingleDerivedPath SingleDerivedPath::parse(
    const Store & store,
    std::string_view s,
    const ExperimentalFeatureSettings & xpSettings)
{
    return parseDerivedPath<SingleDerivedPath>(store, s, "^", xpSettings);
}

SingleDerivedPath SingleDerivedPath::parseLegacy(
    const Store & store,
    std::string_view s,
    const ExperimentalFeatureSettings & xpSettings)
{
    return parseDerivedPath<SingleDerivedPath>(store, s, "!", xpSettings);
}

DerivedPath DerivedPath::parse(
    const Store & store,
    std::string_view s,
    const ExperimentalFeatureSettings & xpSettings)
{
    return parseDerivedPath<DerivedPath>(store, s, "^", xpSettings);
}

DerivedPath DerivedPath::parseLegacy(
    const Store & store,
    std::string_view s,
    const ExperimentalFeatureSettings & xpSettings)
{
    return parseDerivedPath<DerivedPath>(store, s, "!", xpSettings);
}

DerivedPath DerivedPath::fromSingle(const SingleDerivedPath & req)
{
    return std::visit(overloaded {
        [&](const SingleDerivedPath::Opaque & o) -> DerivedPath {
            return o;
        },
        [&](const SingleDerivedPath::Built & b) -> DerivedPath {
            return DerivedPath::Built {
                .drvPath = b.drvPath,
                .outputs = OutputsSpec::Names { b.output },
            };
        },
    }, req.raw());
}

const StorePath & SingleDerivedPath::Built::getBaseStorePath() const
{
	return drvPath->getBaseStorePath();
}

const StorePath & DerivedPath::Built::getBaseStorePath() const
{
	return drvPath->getBaseStorePath();
}

template<typename DP>
static inline const StorePath & getBaseStorePath_(const DP & derivedPath)
{
    return std::visit(overloaded {
        [&](const typename DP::Built & bfd) -> auto & {
            return bfd.drvPath->getBaseStorePath();
        },
        [&](const typename DP::Opaque & bo) -> auto & {
            return bo.path;
        },
    }, derivedPath.raw());
}

const StorePath & SingleDerivedPath::getBaseStorePath() const
{
	return getBaseStorePath_(*this);
}

const StorePath & DerivedPath::getBaseStorePath() const
{
	return getBaseStorePath_(*this);
}

}
