#pragma once
///@file

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

kj::Promise<Result<ref<Store>>> openStore(rust::Ref<Machine> m);

typedef rust::Vec<Machine> Machines;

Machines getMachines();

}
