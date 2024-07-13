#pragma once
///@file

#include "error.hh"
#include "types.hh"

namespace nix {

/**
 * The list of available deprecated features.
 *
 * If you update this, don’t forget to also change the map defining
 * their string representation and documentation in the corresponding
 * `.cc` file as well.
 *
 * Reminder: New deprecated features should start out with a warning without throwing an error.
 * See the developer documentation for details.
 */
enum struct DeprecatedFeature
{
    UrlLiterals
};

/**
 * Just because writing `DeprecatedFeature::UrlLiterals` is way too long
 */
using Dep = DeprecatedFeature;

/**
 * Parse a deprecated feature (enum value) from its name. Deprecated
 * feature flag names are hyphenated and do not contain spaces.
 */
const std::optional<DeprecatedFeature> parseDeprecatedFeature(
        const std::string_view & name);

/**
 * Show the name of a deprecated feature. This is the opposite of
 * parseDeprecatedFeature().
 */
std::string_view showDeprecatedFeature(const DeprecatedFeature);

/**
 * Shorthand for `str << showDeprecatedFeature(feature)`.
 */
std::ostream & operator<<(
        std::ostream & str,
        const DeprecatedFeature & feature);

/**
 * Parse a set of strings to the corresponding set of deprecated
 * features, ignoring (but warning for) any unknown feature.
 */
std::set<DeprecatedFeature> parseDeprecatedFeatures(const std::set<std::string> &);

/**
 * A deprecated feature used for some
 * operation, but was not enabled.
 */
class MissingDeprecatedFeature : public Error
{
public:
    /**
     * The deprecated feature that was required but not enabled.
     */
    DeprecatedFeature missingFeature;

    MissingDeprecatedFeature(DeprecatedFeature missingFeature);
};

}
