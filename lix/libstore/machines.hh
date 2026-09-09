#pragma once
///@file

#include "lix/libstore/store-api.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/ref.hh"
#include "lix/libutil/result.hh"
#include "lix/lix-rs/utils.hh"
#include <kj/async.h>

namespace rust::lix::machines {
struct Machine;
}

namespace nix {
using rust::lix::machines::Machine;

class Store;

kj::Promise<Result<ref<Store>>>
openStore(rust::Ref<Machine> m, StoreConfig::Params storeParams = {}, Activity * parent = nullptr);

typedef rust::Vec<Machine> Machines;

Machines getMachines();

}
