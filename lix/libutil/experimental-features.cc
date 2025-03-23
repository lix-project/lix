#include "lix/libutil/experimental-features.hh"
#include "lix/libutil/json.hh"
#include "lix/libutil/strings.hh"

namespace nix {

struct ExperimentalFeatureDetails
{
    ExperimentalFeature tag;
    std::string_view name;
    std::string_view description;
};

/**
 * If two different PRs both add an experimental feature, and we just
 * used a number for this, we *woudln't* get merge conflict and the
 * counter will be incremented once instead of twice, causing a build
 * failure.
 *
 * By instead defining this instead as a dummy bottom experimental
 * feature, we do not get this issue.
 */
constexpr size_t numXpFeatures = static_cast<size_t>(Xp::NumXpFeatures);

constexpr std::array<ExperimentalFeatureDetails, numXpFeatures> xpFeatureDetails = {{
    #include "experimental-features-impl.gen.inc"
}};

static_assert(
    []() constexpr {
        for (auto [index, feature] : enumerate(xpFeatureDetails))
            if (index != (size_t)feature.tag)
                return false;
        return true;
    }(),
    "array order does not match enum tag order");

const std::optional<ExperimentalFeature> parseExperimentalFeature(const std::string_view & name)
{
    using ReverseXpMap = std::map<std::string_view, ExperimentalFeature>;

    static std::unique_ptr<ReverseXpMap> reverseXpMap = []() {
        auto reverseXpMap = std::make_unique<ReverseXpMap>();
        for (auto & xpFeature : xpFeatureDetails)
            (*reverseXpMap)[xpFeature.name] = xpFeature.tag;
        return reverseXpMap;
    }();

    if (auto feature = get(*reverseXpMap, name))
        return *feature;
    else
        return std::nullopt;
}

std::string_view showExperimentalFeature(const ExperimentalFeature tag)
{
    assert((size_t)tag < xpFeatureDetails.size());
    return xpFeatureDetails[(size_t)tag].name;
}

JSON documentExperimentalFeatures()
{
    StringMap res;
    for (auto & xpFeature : xpFeatureDetails)
        res[std::string { xpFeature.name }] =
            trim(stripIndentation(xpFeature.description));
    return (JSON) res;
}

ExperimentalFeatures parseFeatures(const std::set<std::string> & rawFeatures)
{
    ExperimentalFeatures res {};
    for (auto & rawFeature : rawFeatures)
        if (auto feature = parseExperimentalFeature(rawFeature))
            res = res | *feature;
    return res;
}

MissingExperimentalFeature::MissingExperimentalFeature(ExperimentalFeature feature)
    : Error("experimental Lix feature '%1%' is disabled; use '--extra-experimental-features %1%' to override", showExperimentalFeature(feature))
    , missingFeature(feature)
{}

std::ostream & operator <<(std::ostream & str, const ExperimentalFeature & feature)
{
    return str << showExperimentalFeature(feature);
}

void to_json(JSON & j, const ExperimentalFeature & feature)
{
    j = showExperimentalFeature(feature);
}

void from_json(const JSON & j, ExperimentalFeature & feature)
{
    const std::string input = j;
    const auto parsed = parseExperimentalFeature(input);

    if (parsed.has_value())
        feature = *parsed;
    else
        throw Error("Unknown experimental feature '%s' in JSON input", input);
}

void to_json(JSON & j, const ExperimentalFeatures & f)
{
    StringSet res;
    for (auto & xpFeature : xpFeatureDetails) {
        if ((f & xpFeature.tag) == (ExperimentalFeatures{} | xpFeature.tag)) {
            res.emplace(xpFeature.name);
        }
    }
    j = res;
}

void from_json(const JSON & j, ExperimentalFeatures & f)
{
    f = parseFeatures(j.get<StringSet>());
}

}
