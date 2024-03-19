#pragma once
///@file

#include "experimental-features.hh"
#include "json-utils.hh"

namespace nix {

/**
 * Compute the documentation of all experimental features.
 *
 * See `doc/manual` for how this information is used.
 */
nlohmann::json documentExperimentalFeatures();

/**
 * Semi-magic conversion to and from json.
 * See the nlohmann/json readme for more details.
 */
void to_json(nlohmann::json &, const ExperimentalFeature &);
void from_json(const nlohmann::json &, ExperimentalFeature &);

/**
 * It is always rendered as a string
 */
template<>
struct json_avoids_null<ExperimentalFeature> : std::true_type {};

};
