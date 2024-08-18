#pragma once
///@file

#include "error.hh"
#include "types.hh"

namespace nix {

/**
 * The list of available experimental features.
 *
 * If you update this, don’t forget to also change the map defining
 * their string representation and documentation in the corresponding
 * `.cc` file as well.
 */
enum struct ExperimentalFeature
{
    CaDerivations,
    ImpureDerivations,
    Flakes,
    NixCommand,
    RecursiveNix,
    NoUrlLiterals,
    PipeOperator,
    FetchClosure,
    ReplFlake,
    AutoAllocateUids,
    Cgroups,
    DaemonTrustOverride,
    DynamicDerivations,
    ParseTomlTimestamps,
    ReadOnlyLocalStore,
    ReplAutomation,
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
