#include "deprecated-features.hh"
// Required for instances of to_json and from_json for DeprecatedFeature
#include "deprecated-features-json.hh"
#include "strings.hh"

#include "nlohmann/json.hpp"

namespace nix {

struct DeprecatedFeatureDetails
{
    DeprecatedFeature tag;
    std::string_view name;
    std::string_view description;
};

/**
 * If two different PRs both add a deprecated feature, and we just
 * used a number for this, we *woudln't* get merge conflict and the
 * counter will be incremented once instead of twice, causing a build
 * failure.
 *
 * By instead defining this instead as 1 + the bottom deprecated
 * feature, we either have no issue at all if few features are not added
 * at the end of the list, or a proper merge conflict if they are.
 */
constexpr size_t numDepFeatures = 1 + static_cast<size_t>(Dep::UrlLiterals);

constexpr std::array<DeprecatedFeatureDetails, numDepFeatures> depFeatureDetails = {{
    {
        .tag = Dep::UrlLiterals,
        .name = "url-literals",
        .description = R"(
            Allow unquoted URLs as part of the Nix language syntax.
        )",
    },
}};

static_assert(
    []() constexpr {
        for (auto [index, feature] : enumerate(depFeatureDetails))
            if (index != (size_t)feature.tag)
                return false;
        return true;
    }(),
    "array order does not match enum tag order");

const std::optional<DeprecatedFeature> parseDeprecatedFeature(const std::string_view & name)
{
    using ReverseDepMap = std::map<std::string_view, DeprecatedFeature>;

    static std::unique_ptr<ReverseDepMap> reverseDepMap = []() {
        auto reverseDepMap = std::make_unique<ReverseDepMap>();
        for (auto & depFeature : depFeatureDetails)
            (*reverseDepMap)[depFeature.name] = depFeature.tag;
        return reverseDepMap;
    }();

    if (auto feature = get(*reverseDepMap, name))
        return *feature;
    else
        return std::nullopt;
}

std::string_view showDeprecatedFeature(const DeprecatedFeature tag)
{
    assert((size_t)tag < depFeatureDetails.size());
    return depFeatureDetails[(size_t)tag].name;
}

nlohmann::json documentDeprecatedFeatures()
{
    StringMap res;
    for (auto & depFeature : depFeatureDetails)
        res[std::string { depFeature.name }] =
            trim(stripIndentation(depFeature.description));
    return (nlohmann::json) res;
}

std::set<DeprecatedFeature> parseDeprecatedFeatures(const std::set<std::string> & rawFeatures)
{
    std::set<DeprecatedFeature> res;
    for (auto & rawFeature : rawFeatures)
        if (auto feature = parseDeprecatedFeature(rawFeature))
            res.insert(*feature);
    return res;
}

MissingDeprecatedFeature::MissingDeprecatedFeature(DeprecatedFeature feature)
    : Error("Lix feature '%1%' is deprecated and should not be used anymore; use '--extra-deprecated-features %1%' to disable this error", showDeprecatedFeature(feature))
    , missingFeature(feature)
{}

std::ostream & operator <<(std::ostream & str, const DeprecatedFeature & feature)
{
    return str << showDeprecatedFeature(feature);
}

void to_json(nlohmann::json & j, const DeprecatedFeature & feature)
{
    j = showDeprecatedFeature(feature);
}

void from_json(const nlohmann::json & j, DeprecatedFeature & feature)
{
    const std::string input = j;
    const auto parsed = parseDeprecatedFeature(input);

    if (parsed.has_value())
        feature = *parsed;
    else
        throw Error("Unknown deprecated feature '%s' in JSON input", input);
}

}
