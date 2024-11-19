#pragma once
///@file

#include "lix/libstore/store-api.hh"

namespace nix {

void printDotGraph(ref<Store> store, StorePathSet && roots);

}
