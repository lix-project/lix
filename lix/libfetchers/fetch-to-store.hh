#pragma once
///@file

#include "lix/libutil/source-path.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/repair-flag.hh"
#include "lix/libstore/content-address.hh"

namespace nix {

/**
 * Copy the `path` to the Nix store.
 */
kj::Promise<Result<StorePath>> fetchToStore(
    Store & store,
    const CheckedSourcePath & path,
    std::string_view name = "source",
    FileIngestionMethod method = FileIngestionMethod::Recursive,
    PathFilter * filter = nullptr,
    RepairFlag repair = NoRepair);

}
