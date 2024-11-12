#pragma once
///@file

#include <regex>
#include <string>
#include <vector>

#include "lix/libutil/error.hh"

namespace nix {

class ShlexError : public Error
{
public:
    const std::string input;

    ShlexError(const std::string input)
        : Error("Failed to parse shell arguments (unterminated quote?): %1%", input)
        , input(input)
    {
    }
};

/**
 * Parse a string into shell arguments.
 *
 * Takes care of whitespace, quotes, and backslashes (at least a bit).
 */
std::vector<std::string> shell_split(const std::string & input);

} // namespace nix
