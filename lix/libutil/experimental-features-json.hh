#pragma once
///@file

#include "lix/libutil/experimental-features.hh"
#include "lix/libutil/json-fwd.hh"

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

};
