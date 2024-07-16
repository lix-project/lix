#pragma once
/// @file Defines C++ 20 concepts that std doesn't have.

#include <type_traits>

namespace nix
{

/// Like std::invocable<>, but also constrains the return type as well.
///
/// Somehow, there is no std concept to do this, even though there is a type trait
/// for it.
///
/// @tparam CallableT The type you want to constrain to be callable, and to return
/// @p ReturnT when called with @p Args as arguments.
///
/// @tparam ReturnT The type the callable should return when called.
/// @tparam Args The arguments the callable should accept to return @p ReturnT.
template<typename CallableT, typename ReturnT, typename ...Args>
concept InvocableR = std::is_invocable_r_v<ReturnT, CallableT, Args...>;

}
