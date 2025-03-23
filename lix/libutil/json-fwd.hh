#pragma once
///@file Lix-specific JSON handling (forward declarations only).

#include <nlohmann/json_fwd.hpp>
#include <type_traits>

namespace nix {

namespace json {

/**
 * For `adl_serializer<std::optional<T>>` below, we need to track what
 * types are not already using `null`. Only for them can we use `null`
 * to represent `std::nullopt`.
 */
template<typename T>
struct avoids_null;

template<typename T>
struct is_integral_enum : std::false_type
{};

template<typename T>
concept IntegralEnum = is_integral_enum<T>::value;

template<typename T = void, typename SFINAE = void>
struct adl_serializer;

}

/**
 * Specialization of `nlohmann::basic_json`. We do not use `nlohmann::json`
 * because we want full control over the default serializer without needing
 * to force users of lix as a library to use our customized serializer code
 * as well, such as our specializations for `std::optional<T>` with checks.
 */
using JSON = nlohmann::basic_json<
    std::map,
    std::vector,
    std::string,
    bool,
    std::int64_t,
    std::uint64_t,
    double,
    std::allocator,
    json::adl_serializer>;

const JSON * get(const JSON & map, const std::string & key);

JSON * get(JSON & map, const std::string & key);

/**
 * Get the value of a json object at a key safely, failing
 * with a Nix Error if the key does not exist.
 *
 * Use instead of JSON::at() to avoid ugly exceptions.
 *
 * _Does not check whether `map` is an object_, use `ensureType` for that.
 */
const JSON & valueAt(
    const JSON & map,
    const std::string & key);

}
