#pragma once
/// @file Defines C++ 20 concepts that std doesn't have.

#include <ranges>
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

/// @brief Like std::ranges::viewable_range<>, but also verifies the range's value type.
///
/// Because C++ sucks ass, the check you want against the range's value type
/// might vary wildly. So this concept takes a *template template* argument,
/// which should be a type-trait like template-struct, which takes two template type
/// arguments, the first being std::ranges::range_value_t<RangeT>, and the other being
/// ElemT.
///
/// @tparam RangeT The range type you want to constrain.
/// @tparam ItemT The item type you want RangeT's item type to be "like".
/// @tparam LikeT The to check if RangeT's item type is "like" ItemT.
template<typename RangeT, typename ItemT, template<typename, typename> typename LikeT>
concept ViewOfLike =
    std::ranges::viewable_range<RangeT> && LikeT<std::ranges::range_value_t<RangeT>, ItemT>::value;

/// @brief Application of @ref ViewOfLike using @ref std::is_nothrow_convertible.
///
/// @tparam RangeT The range type to constrain.
/// @tparam ItemT The item type you want RangeT's item type to be nothrow-convertible to.
template<typename RangeT, typename ItemT>
concept ViewOf = ViewOfLike<RangeT, ItemT, std::is_nothrow_convertible>;
}
