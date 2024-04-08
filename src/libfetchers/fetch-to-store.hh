#pragma once
///@file

#include "source-path.hh"
#include "store-api.hh"
#include "util.hh"
#include "repair-flag.hh"
#include "content-address.hh"

namespace nix {

/**
 * Copy the `path` to the Nix store.
 */
StorePath fetchToStore(
    Store & store,
    const SourcePath & path,
    std::string_view name = "source",
    FileIngestionMethod method = FileIngestionMethod::Recursive,
    PathFilter * filter = nullptr,
    RepairFlag repair = NoRepair);

}
