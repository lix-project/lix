#pragma once
///@file
#include "lix/libstore/derived-path.hh"
#include "lix/libstore/realisation.hh"
#include "lix/libutil/result.hh"
#include <kj/async.h>

namespace nix {

struct SingleBuiltPath;

struct SingleBuiltPathBuilt {
    ref<SingleBuiltPath> drvPath;
    std::pair<std::string, StorePath> output;

    SingleDerivedPathBuilt discardOutputPath() const;

    std::string to_string(const Store & store) const;
    static SingleBuiltPathBuilt parse(const Store & store, std::string_view, std::string_view);
    kj::Promise<Result<nlohmann::json>> toJSON(const Store & store) const;

    DECLARE_CMP(SingleBuiltPathBuilt);
};

namespace built_path::detail {
using SingleBuiltPathRaw = std::variant<
    DerivedPathOpaque,
    SingleBuiltPathBuilt
>;
}

struct SingleBuiltPath : built_path::detail::SingleBuiltPathRaw {
    using Raw = built_path::detail::SingleBuiltPathRaw;
    using Raw::Raw;

    using Opaque = DerivedPathOpaque;
    using Built = SingleBuiltPathBuilt;

    inline const Raw & raw() const {
        return static_cast<const Raw &>(*this);
    }

    StorePath outPath() const;

    SingleDerivedPath discardOutputPath() const;

    static SingleBuiltPath parse(const Store & store, std::string_view);
    kj::Promise<Result<nlohmann::json>> toJSON(const Store & store) const;
};

static inline ref<SingleBuiltPath> staticDrv(StorePath drvPath)
{
    return make_ref<SingleBuiltPath>(SingleBuiltPath::Opaque { drvPath });
}

/**
 * A built derived path with hints in the form of optional concrete output paths.
 *
 * See 'BuiltPath' for more an explanation.
 */
struct BuiltPathBuilt {
    ref<SingleBuiltPath> drvPath;
    std::map<std::string, StorePath> outputs;

    std::string to_string(const Store & store) const;
    static BuiltPathBuilt parse(const Store & store, std::string_view, std::string_view);
    kj::Promise<Result<nlohmann::json>> toJSON(const Store & store) const;

    DECLARE_CMP(BuiltPathBuilt);
};

namespace built_path::detail {
using BuiltPathRaw = std::variant<
    DerivedPath::Opaque,
    BuiltPathBuilt
>;
}

/**
 * A built path. Similar to a DerivedPath, but enriched with the corresponding
 * output path(s).
 */
struct BuiltPath : built_path::detail::BuiltPathRaw {
    using Raw = built_path::detail::BuiltPathRaw;
    using Raw::Raw;

    using Opaque = DerivedPathOpaque;
    using Built = BuiltPathBuilt;

    inline const Raw & raw() const {
        return static_cast<const Raw &>(*this);
    }

    StorePathSet outPaths() const;
    kj::Promise<Result<RealisedPath::Set>> toRealisedPaths(Store & store) const;

    kj::Promise<Result<nlohmann::json>> toJSON(const Store & store) const;
};

typedef std::vector<BuiltPath> BuiltPaths;

}
