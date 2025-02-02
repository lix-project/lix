#pragma once
///@file

#include "lix/libstore/store-api.hh"

namespace nix {

kj::Promise<Result<void>> printDotGraph(ref<Store> store, StorePathSet && roots);

}
