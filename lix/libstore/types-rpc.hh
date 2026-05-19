#pragma once
///@file RPC helper functions for `types.hh`

#include "lix/libstore/types.capnp.h"
#include "lix/libutil/rpc.hh"
#include "path.hh"
#include "store-api.hh"
#include <string_view>

namespace nix::rpc {

namespace detail {
template<typename Arg, typename... Args>
Store & storeOf(Arg && s, Args &&... args)
{
    if constexpr (requires { static_cast<Store &>(s); }) {
        static_assert(!(requires { static_cast<Store &>(args); } || ...), "only one store parameter allowed");
        return s;
    } else {
        return storeOf(args...);
    }
}
}

inline nix::StorePath from(const StorePath::Reader & sp, auto &&... args)
{
    Store & store = detail::storeOf(args...);
    return store.parseStorePath(to<std::string_view>(sp.getRaw()));
}

template<>
struct Fill<StorePath, nix::StorePath>
{
    static void fill(StorePath::Builder spb, const nix::StorePath & sp, auto &&... args)
    {
        Store & store = detail::storeOf(args...);
        LIX_RPC_FILL(spb, setRaw, store.printStorePath(sp));
    }
};
}
