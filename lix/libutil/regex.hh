#pragma once
///@file

#include <string>
#include <regex>

namespace nix::regex {
std::string quoteRegexChars(const std::string & raw);

std::regex storePathRegex(const std::string & storeDir);
}
