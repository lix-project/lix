#pragma once
///@file RPC helper forward declarations

namespace nix::rpc {
template<typename From, typename To>
struct Convert;

template<typename To, typename From>
struct Fill;
}
