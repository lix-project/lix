#pragma once
/// @file

#include "fs-accessor.hh"
#include "lix/libutil/result.hh"
#include "path.hh"
#include "store-api.hh"
#include <kj/async.h>

#define ANSI_ALREADY_VISITED "\e[38;5;244m"

namespace nix {
kj::Promise<Result<std::string>> genGraphString(
    const StorePath & start,
    const StorePath & to,
    const std::map<StorePath, StorePathSet> & graphData,
    Store & store,
    bool all,
    bool precise,
    std::optional<ref<FSAccessor>> accessor = std::nullopt
);
}
