#pragma once
///@file

#include "error.hh"
#include <string>
#include <regex>

namespace nix::regex {
class Error : public nix::Error
{
public:
    using nix::Error::Error;
};

std::string quoteRegexChars(const std::string & raw);

std::regex storePathRegex(const std::string & storeDir);

std::regex parse(std::string_view re, std::regex::flag_type flags = std::regex::ECMAScript);
}
