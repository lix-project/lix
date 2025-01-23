#pragma once
///@file

#include "lix/libutil/async.hh"
#include "lix/libutil/serialise.hh"
#include "lix/libstore/store-api.hh"

namespace nix::daemon {

enum RecursiveFlag : bool { NotRecursive = false, Recursive = true };

void processConnection(
    AsyncIoRoot & aio,
    ref<Store> store,
    FdSource & from,
    FdSink & to,
    TrustedFlag trusted,
    RecursiveFlag recursive);

}
