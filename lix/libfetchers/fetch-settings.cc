#include "lix/libutil/abstract-setting-to-json.hh"
#include "lix/libutil/args.hh"
#include "lix/libutil/config-impl.hh"
#include "lix/libutil/json.hh"
#include "lix/libfetchers/fetch-settings.hh"

namespace nix {

void to_json(JSON & j, const AcceptFlakeConfig & e)
{
    if (e == AcceptFlakeConfig::False) {
        j = false;
    } else if (e == AcceptFlakeConfig::Ask) {
        j = "ask";
    } else if (e == AcceptFlakeConfig::True) {
        j = true;
    } else {
        abort();
    }
}

void from_json(const JSON & j, AcceptFlakeConfig & e)
{
    if (j == false) {
        e = AcceptFlakeConfig::False;
    } else if (j == "ask") {
        e = AcceptFlakeConfig::Ask;
    } else if (j == true) {
        e = AcceptFlakeConfig::True;
    } else {
        throw Error("Invalid accept-flake-config value '%s'", std::string(j));
    }
}

template<> AcceptFlakeConfig BaseSetting<AcceptFlakeConfig>::parse(const std::string & str, const ApplyConfigOptions & options) const
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
