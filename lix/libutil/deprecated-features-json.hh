#pragma once
///@file

#include "lix/libutil/deprecated-features.hh"
#include "lix/libutil/json-fwd.hh"

namespace nix {

/**
 * Compute the documentation of all deprecated features.
 *
 * See `doc/manual` for how this information is used.
 */
JSON documentDeprecatedFeatures();

/**
 * Semi-magic conversion to and from json.
 * See the nlohmann/json readme for more details.
 */
void to_json(JSON &, const DeprecatedFeature &);
void from_json(const JSON &, DeprecatedFeature &);

};
