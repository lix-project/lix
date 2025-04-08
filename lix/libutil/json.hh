#pragma once
/// @file Lix-specific JSON handling. We do not use plain `nlohmann::json`
/// because we want to override serializer behavior without imposing these
/// overrides on out-of-tree users of libutil, as we'd be required to when
/// specializing templates in the nlohmann namespace. nlohmann::json can't
/// deal with `std::optional<T>` types until 3.11.3 at, and we need those.
/// We also customize enum serialization to not automatically cast to int;
/// nlohmann can be told to disable this only via a special global define.

#include "lix/libutil/error.hh"
#include "lix/libutil/fmt.hh"
#include "lix/libutil/json-fwd.hh" // IWYU pragma: export
#include <concepts>
#include <nlohmann/json.hpp> // IWYU pragma: export
#include <list>
#include <string_view>
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
    : std::bool_constant<std::is_integral_v<T> || std::is_floating_point_v<T> || IntegralEnum<T>>
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

    // Following https://github.com/nlohmann/json#how-can-i-use-get-for-non-default-constructiblenon-copyable-types
    template<typename Json>
        requires requires(Json & j, T value) { T::to_json(j, value); }
    static void to_json(Json & j, const T & value)
    {
        T::to_json(j, value);
    }

    template<typename Json>
        requires requires(Json && j) {
            {
                T::from_json(std::forward<Json>(j))
            } -> std::same_as<T>;
        }
    static auto from_json(Json && j)
    {
        return T::from_json(std::forward<Json>(j));
    }

    template<typename Json>
        requires IntegralEnum<T>
    static void to_json(Json && json, const T & value)
    {
        json = static_cast<std::underlying_type_t<T>>(value);
    }

    template<typename Json>
        requires IntegralEnum<T>
    static void from_json(const Json & json, T & value)
    {
        value = static_cast<T>(json.template get<std::underlying_type_t<T>>());
    }
};

template<typename T>
struct adl_serializer<std::optional<T>>
{
    /**
     * @brief Convert a JSON type to an `optional<T>` treating
     *        `null` as `std::nullopt`.
     */
    static void from_json(const auto & json, std::optional<T> & t)
    {
        static_assert(avoids_null<T>::value, "null is already in use for underlying type's JSON");
        t = json.is_null() ? std::nullopt : std::make_optional(json.template get<T>());
    }

    /**
     *  @brief Convert an optional type to a JSON type  treating `std::nullopt`
     *         as `null`.
     */
    static void to_json(auto & json, const std::optional<T> & t)
    {
        static_assert(avoids_null<T>::value, "null is already in use for underlying type's JSON");
        if (t) {
            json = *t;
        } else {
            json = nullptr;
        }
    }
};

MakeError(ParseError, Error);

/**
 * Parse some JSON and throw an `Error` on failure. This make nlohmann errors
 * more inspectable and lets us add meaningful backtraces to any json errors.
 */
template<typename Source>
JSON parse(Source && source, std::optional<std::string_view> context = {})
{
    try {
        // NOLINTNEXTLINE(lix-disallowed-decls): this is the wrapper for that
        return JSON::parse(std::forward<Source>(source));
    } catch (JSON::exception & e) { // NOLINT(lix-foreign-exceptions)
        ParseError error{"failed to parse JSON: %s", e.what()};
        if (context) {
            error.addTrace(nullptr, fmt("while parsing %s", *context));
        }
        throw error;
    }
}
}

}
