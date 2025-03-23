#pragma once
///@file

#include "lix/libutil/json.hh"

/**
 * This "instance" is widely requested, see
 * https://github.com/nlohmann/json/issues/1749, but momentum has stalled
 * out. Writing there here in Nix as a stop-gap.
 *
 * We need to make sure the underlying type does not use `null` for this to
 * round trip. We do that with a static assert.
 */
template<typename T>
struct nix::json::adl_serializer<std::optional<T>> {
    /**
     * @brief Convert a JSON type to an `optional<T>` treating
     *        `null` as `std::nullopt`.
     */
    static void from_json(const auto & json, std::optional<T> & t) {
        static_assert(
            nix::json::avoids_null<T>::value,
            "null is already in use for underlying type's JSON");
        t = json.is_null()
            ? std::nullopt
            : std::make_optional(json.template get<T>());
    }

    /**
     *  @brief Convert an optional type to a JSON type  treating `std::nullopt`
     *         as `null`.
     */
    static void to_json(auto & json, const std::optional<T> & t) {
        static_assert(
            nix::json::avoids_null<T>::value,
            "null is already in use for underlying type's JSON");
        if (t)
            json = *t;
        else
            json = nullptr;
    }
};
