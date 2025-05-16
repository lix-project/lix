#include "lix/libutil/json.hh"
#include "lix/libstore/derived-path.hh"
#include "lix/libstore/store-api.hh"

#include <optional>

namespace nix {

#define CMP_ONE(CHILD_TYPE, MY_TYPE, FIELD, COMPARATOR) \
    bool MY_TYPE ::operator COMPARATOR (const MY_TYPE & other) const \
    { \
        const MY_TYPE* me = this; \
        auto fields1 = std::tie(me->drvPath, me->FIELD); \
        me = &other; \
        auto fields2 = std::tie(me->drvPath, me->FIELD); \
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

kj::Promise<Result<JSON>> DerivedPath::Built::toJSON(Store & store) const
try {
    JSON res;
    res["drvPath"] = TRY_AWAIT(drvPath.toJSON(store));
    // Fallback for the input-addressed derivation case: We expect to always be
    // able to print the output paths, so let’s do it
    const auto outputMap = TRY_AWAIT(store.queryDerivationOutputMap(drvPath.path));
    for (const auto & [output, outputPathOpt] : outputMap) {
        if (!outputs.contains(output)) continue;
        res["outputs"][output] = store.printStorePath(outputPathOpt);
    }
    co_return res;
} catch (...) {
    co_return result::current_exception();
}

std::string DerivedPath::Opaque::to_string(const Store & store) const
{
    return store.printStorePath(path);
}

std::string DerivedPath::Built::to_string(const Store & store) const
{
    return drvPath.to_string(store)
        + '^'
        + outputs.to_string();
}

std::string DerivedPath::Built::to_string_legacy(const Store & store) const
{
    return drvPath.to_string(store)
        + "!"
        + outputs.to_string();
}

std::string DerivedPath::to_string(const Store & store) const
{
    return std::visit(
        [&](const auto & req) { return req.to_string(store); },
        raw());
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

DerivedPath::Built DerivedPath::Built::parse(
    const Store & store, DerivedPathOpaque drv,
    OutputNameView outputsS)
{
    return {
        .drvPath = std::move(drv),
        .outputs = OutputsSpec::parse(outputsS),
    };
}

template <typename DerivedPathT>
static DerivedPathT parseDerivedPath(
    const Store & store, std::string_view s, std::string_view separator)
{
    size_t n = s.rfind(separator);
    if (n == s.npos) {
        return DerivedPathT::Opaque::parse(store, s);
    } else {
        auto path = DerivedPathT::Built::parse(store,
            DerivedPathT::Opaque::parse(store, s.substr(0, n)),
            s.substr(n + 1));

        const auto& basePath = path.drvPath.path;
        if (!basePath.isDerivation()) {
            throw InvalidPath("cannot use output selection ('%s') on non-derivation store path '%s'",
                              separator, basePath.to_string());
        }

        return path;
    }
}

DerivedPath DerivedPath::parseLegacy(const Store & store, std::string_view s)
{
    return parseDerivedPath<DerivedPath>(store, s, "!");
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

template<typename DP>
static inline const StorePath & getBaseStorePath_(const DP & derivedPath)
{
    return std::visit(overloaded {
        [&](const typename DP::Built & bfd) -> const StorePath & {
            return bfd.drvPath.path;
        },
        [&](const typename DP::Opaque & bo) -> const StorePath & {
            return bo.path;
        },
    }, derivedPath.raw());
}

const StorePath & DerivedPath::getBaseStorePath() const
{
	return getBaseStorePath_(*this);
}

}
