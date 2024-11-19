#pragma once
///@file

#include "lix/libstore/store-api.hh"

namespace nix {

void printGraphML(ref<Store> store, StorePathSet && roots);

}
