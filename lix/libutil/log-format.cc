#include "lix/libutil/log-format.hh"

#include "lix/libutil/abstract-setting-to-json.hh" // IWYU pragma: keep
#include "lix/libutil/json.hh"
#include "lix/libutil/log-format.hh"

#include <format>

namespace nix {
template<>
std::string BaseSetting<LogFormat>::to_string() const
{
    return std::format("{}", value);
}

template<>
LogFormat
BaseSetting<LogFormat>::parse(const std::string & str, const ApplyConfigOptions & options) const
{
    if (auto const parsed = LogFormat::parse(str)) {
        return *parsed;
    }
    throw UsageError("setting '%s' has invalid value '%s'", name, str);
}

void to_json(JSON & j, const LogFormat & self)
{
    j = std::format("{}", self);
}

void from_json(const JSON & j, LogFormat & self)
{
    std::string asStr = ensureType(j, JSON::value_t::string);
    auto const parsed = LogFormat::parse(asStr);
    if (!parsed) {
        throw Error("invalid json for 'log-format': %s", j);
    }
    self = *parsed;
}

// Explicitly instantiate the non-specialized templates.
// `abstract-setting-to-json.hh` is IWYU-kept so this line also instantiates that template.
template class BaseSetting<LogFormat>;

}

static_assert(std::formattable<nix::LogFormat, char>);
static_assert(std::formattable<nix::LogFormatValue, char>);
