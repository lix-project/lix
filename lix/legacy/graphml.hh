#pragma once
///@file

#include "lix/libstore/store-api.hh"

namespace nix {

kj::Promise<Result<void>> printGraphML(ref<Store> store, StorePathSet && roots);

}
