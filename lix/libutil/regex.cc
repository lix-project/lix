#include "regex.hh"
#include <string>
#include <regex>

// Declared as extern in precompiled-headers.hh
template class std::basic_regex<char>;

namespace nix::regex {
std::string quoteRegexChars(const std::string & raw)
{
    static auto specialRegex = parse(R"([.^$\\*+?()\[\]{}|])");
    return std::regex_replace(raw, specialRegex, R"(\$&)");
}

std::regex storePathRegex(const std::string & storeDir)
{
    return parse(quoteRegexChars(storeDir) + R"(/[0-9a-z]+[0-9a-zA-Z\+\-\._\?=]*)");
}

std::regex parse(std::string_view re, std::regex::flag_type flags)
try {
    return std::regex(re.begin(), re.end(), flags); // NOLINT: lix-foreign-exceptions
} catch (std::regex_error & e) { // NOLINT: lix-foreign-exceptions
    if (e.code() == std::regex_constants::error_space) {
        // limit is _GLIBCXX_REGEX_STATE_LIMIT for libstdc++
        throw Error("memory limit exceeded by regular expression '%s'", re);
    } else {
        throw Error("invalid regular expression '%s': %s", re, e.what());
    }
}
}
