#include "abstract-setting-to-json.hh"
#include "args.hh"
#include "config-impl.hh"
#include "fetch-settings.hh"

#include <nlohmann/json.hpp>

namespace nix {

template<> AcceptFlakeConfig BaseSetting<AcceptFlakeConfig>::parse(const std::string & str) const
{
    if (str == "true") return AcceptFlakeConfig::True;
    else if (str == "ask") return AcceptFlakeConfig::Ask;
    else if (str == "false") return AcceptFlakeConfig::False;
    else throw UsageError("option '%s' has invalid value '%s'", name, str);
}

template<> std::string BaseSetting<AcceptFlakeConfig>::to_string() const
{
    if (value == AcceptFlakeConfig::True) return "true";
    else if (value == AcceptFlakeConfig::Ask) return "ask";
    else if (value == AcceptFlakeConfig::False) return "false";
    else abort();
}

template<> void BaseSetting<AcceptFlakeConfig>::convertToArg(Args & args, const std::string & category)
{
    args.addFlag({
        .longName = name,
        .description = "Accept Lix configuration options from flakes without confirmation. This allows flakes to gain root access to your machine if you are a trusted user; see the nix.conf manual page for more details.",
        .category = category,
        .handler = {[this]() { override(AcceptFlakeConfig::True); }}
    });
    args.addFlag({
        .longName = "ask-" + name,
        .description = "Ask whether to accept Lix configuration options from flakes.",
        .category = category,
        .handler = {[this]() { override(AcceptFlakeConfig::Ask); }}
    });
    args.addFlag({
        .longName = "no-" + name,
        .description = "Reject Lix configuration options from flakes.",
        .category = category,
        .handler = {[this]() { override(AcceptFlakeConfig::False); }}
    });
}

FetchSettings::FetchSettings()
{
}

FetchSettings fetchSettings;

static GlobalConfig::Register rFetchSettings(&fetchSettings);

}
