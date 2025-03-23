#pragma once
///@file

#include "lix/libutil/experimental-features.hh"
#include "lix/libutil/json-utils.hh"

namespace nix {

/**
 * Compute the documentation of all experimental features.
 *
 * See `doc/manual` for how this information is used.
 */
JSON documentExperimentalFeatures();

/**
 * Semi-magic conversion to and from json.
 * See the nlohmann/json readme for more details.
 */
void to_json(JSON &, const ExperimentalFeature &);
void from_json(const JSON &, ExperimentalFeature &);

/**
 * It is always rendered as a string
 */
template<>
struct json_avoids_null<ExperimentalFeature> : std::true_type {};

};
