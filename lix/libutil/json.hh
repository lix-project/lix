#pragma once
/// @file Lix-specific JSON handling. We do not use plain `nlohmann::json`
/// because we want to override serializer behavior without imposing these
/// overrides on out-of-tree users of libutil, as we'd be required to when
/// specializing templates in the nlohmann namespace. nlohmann::json can't
/// deal with `std::optional<T>` types until 3.11.3 at, and we need those.

#include "lix/libutil/json-fwd.hh" // IWYU pragma: keep
#include <nlohmann/json.hpp> // IWYU pragma: keep
#include <list>
#include <type_traits>

namespace nix {

/**
 * Ensure the type of a json object is what you expect, failing
 * with a Nix Error if it isn't.
 *
 * Use before type conversions and element access to avoid ugly exceptions.
 */
const JSON & ensureType(
    const JSON & value,
    JSON::value_type expectedType);

namespace json {

/**
 * Handle numbers and enums in default impl
 */
template<typename T>
struct avoids_null
    : std::bool_constant<std::is_integral_v<T> || std::is_floating_point_v<T> || std::is_enum_v<T>>
{};

template<>
struct avoids_null<std::nullptr_t> : std::false_type {};

template<>
struct avoids_null<bool> : std::true_type {};

template<>
struct avoids_null<std::string> : std::true_type {};

template<typename T>
struct avoids_null<std::vector<T>> : std::true_type {};

template<typename T>
struct avoids_null<std::list<T>> : std::true_type {};

template<typename K, typename V>
struct avoids_null<std::map<K, V>> : std::true_type {};

namespace detail {
template<typename Json, typename T>
    requires requires(Json & j,  T value) { to_json(j, value); }
void call_to_json(Json & j, const T & value)
{
    to_json(j, value);
}

template<typename Json, typename T>
    requires requires(Json && j, T & value) { from_json(std::forward<Json>(j), value); }
void call_from_json(Json && j, T & value)
{
    from_json(std::forward<Json>(j), value);
}
}

template<typename T>
struct adl_serializer<T, void>
{
    template<typename Json>
        requires requires(Json & j, T value) { detail::call_to_json(j, value); }
    static void to_json(Json & j, const T & value)
    {
        detail::call_to_json(j, value);
    }

    template<typename Json>
        requires requires(Json && j, T & value) {
            detail::call_from_json(std::forward<Json>(j), value);
        }
    static void from_json(Json && j, T & value)
    {
        detail::call_from_json(std::forward<Json>(j), value);
    }
};
}

}
