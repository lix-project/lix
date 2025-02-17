#pragma once
///@file

#include "lix/libutil/deprecated-features.hh"
#include "lix/libutil/json-utils.hh"

namespace nix {

/**
 * Compute the documentation of all deprecated features.
 *
 * See `doc/manual` for how this information is used.
 */
nlohmann::json documentDeprecatedFeatures();

/**
 * Semi-magic conversion to and from json.
 * See the nlohmann/json readme for more details.
 */
void to_json(nlohmann::json &, const DeprecatedFeature &);
void from_json(const nlohmann::json &, DeprecatedFeature &);

/**
 * It is always rendered as a string
 */
template<>
struct json_avoids_null<DeprecatedFeature> : std::true_type {};

};
