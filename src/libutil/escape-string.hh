#pragma once

#include <limits>
#include <ostream>

namespace nix {

/**
 * Escape a string for output.
 *
 * With default optional parameters, the output string will round-trip through
 * the Nix evaluator (i.e. you can copy/paste this function's output into the
 * REPL and have it evaluate as the string that got passed in).
 *
 * With non-default optional parameters, the output string will be
 * human-readable.
 */

std::ostream & escapeString(
    std::ostream & output,
    const std::string_view string,
    size_t maxLength = std::numeric_limits<size_t>::max(),
    bool ansiColors = false
);

/**
 * Escape a string for output, writing the escaped result to a new string.
 */
inline std::ostream & escapeString(std::ostream & output, const char * string)
{
    return escapeString(output, std::string_view(string));
}

} // namespace nix
