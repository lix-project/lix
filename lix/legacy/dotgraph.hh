#pragma once
///@file

#include "lix/libstore/store-api.hh"

namespace nix {

kj::Promise<Result<std::string>> formatDotGraph(ref<Store> store, StorePathSet && roots);
}
