#pragma once
///@file RPC helper functions for `types.hh`

#include "lix/libstore/types.capnp.h"
#include "lix/libutil/rpc.hh"
#include "path.hh"
#include "store-api.hh"
#include <string_view>

namespace nix::rpc {
inline nix::StorePath from(const StorePath::Reader & sp, auto &&... args)
{
    Store & store = std::get<Store &>(std::tie(args...));
    return store.parseStorePath(to<std::string_view>(sp.getRaw()));
}

template<>
struct Fill<StorePath, nix::StorePath>
{
    static void fill(StorePath::Builder spb, const nix::StorePath & sp, auto &&... args)
    {
        Store & store = std::get<Store &>(std::tie(args...));
        LIX_RPC_FILL(spb, setRaw, store.printStorePath(sp));
    }
};
}
