#pragma once
///@file
#include "lix/libstore/derived-path.hh"
#include "lix/libstore/realisation.hh"
#include "lix/libutil/result.hh"
#include <kj/async.h>

namespace nix {

/**
 * A built derived path with hints in the form of optional concrete output paths.
 *
 * See 'BuiltPath' for more an explanation.
 */
struct BuiltPathBuilt {
    DerivedPathOpaque drvPath;
    std::map<std::string, StorePath> outputs;

    kj::Promise<Result<JSON>> toJSON(const Store & store) const;

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
};

typedef std::vector<BuiltPath> BuiltPaths;

}
