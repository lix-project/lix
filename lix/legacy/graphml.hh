#pragma once
///@file

#include "lix/libstore/store-api.hh"

namespace nix {

kj::Promise<Result<std::string>> formatGraphML(ref<Store> store, StorePathSet && roots);
}
