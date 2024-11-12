#pragma once
///@file

#include "lix/libutil/error.hh"
#include "lix/libutil/types.hh"

namespace nix {

/**
 * The list of available experimental features.
 */
enum struct ExperimentalFeature
{
    #include "experimental-features.gen.inc"
    NumXpFeatures, // number of available experimental features, do not use
};

enum struct ExperimentalFeatures {};

inline ExperimentalFeatures operator| (ExperimentalFeatures a, ExperimentalFeatures b) {
    return static_cast<ExperimentalFeatures>(static_cast<size_t>(a) | static_cast<size_t>(b));
}

inline ExperimentalFeatures operator| (ExperimentalFeatures a, ExperimentalFeature b) {
    return a | static_cast<ExperimentalFeatures>(1 << static_cast<size_t>(b));
}

inline ExperimentalFeatures operator& (ExperimentalFeatures a, ExperimentalFeature b) {
    return static_cast<ExperimentalFeatures>(static_cast<size_t>(a) & (1 << static_cast<size_t>(b)));
}

/**
 * Just because writing `ExperimentalFeature::CaDerivations` is way too long
 */
using Xp = ExperimentalFeature;

/**
 * Parse an experimental feature (enum value) from its name. Experimental
 * feature flag names are hyphenated and do not contain spaces.
 */
const std::optional<ExperimentalFeature> parseExperimentalFeature(
        const std::string_view & name);

/**
 * Show the name of an experimental feature. This is the opposite of
 * parseExperimentalFeature().
 */
std::string_view showExperimentalFeature(const ExperimentalFeature);

/**
 * Shorthand for `str << showExperimentalFeature(feature)`.
 */
std::ostream & operator<<(
        std::ostream & str,
        const ExperimentalFeature & feature);

/**
 * Parse a set of strings to the corresponding set of experimental
 * features, ignoring (but warning for) any unknown feature.
 */
ExperimentalFeatures parseFeatures(const std::set<std::string> &);

/**
 * An experimental feature was required for some (experimental)
 * operation, but was not enabled.
 */
class MissingExperimentalFeature : public Error
{
public:
    /**
     * The experimental feature that was required but not enabled.
     */
    ExperimentalFeature missingFeature;

    MissingExperimentalFeature(ExperimentalFeature missingFeature);
};

}
