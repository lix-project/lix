#pragma once
///@file

#include <limits>
#include <ostream>

namespace nix {

/**
 * Options for escaping strings in `escapeString`.
 *
 * With default optional parameters, the output string will round-trip through
 * the Nix evaluator (i.e. you can copy/paste this function's output into the
 * REPL and have it evaluate as the string that got passed in).
 *
 * With non-default optional parameters, the output string will be
 * human-readable.
 */
struct EscapeStringOptions
{
    /**
     * If `maxLength` is decreased, some trailing portion of the string may be
     * omitted with a message like `«123 bytes elided»`.
     */
    size_t maxLength = std::numeric_limits<size_t>::max();

    /**
     * If `outputAnsiColors` is set, the string will be colored the color of literals, using
     * ANSI escape codes.
     */
    bool outputAnsiColors = false;

    /**
     * If `escapeNonPrinting` is set, non-printing ASCII characters (i.e. with
     * byte values less than 0x20) will be printed in `\xhh` format, like
     * `\x1d` (other than those that Nix supports, like `\n`, `\r`, `\t`).
     * Note that this format is not yet supported by the Lix parser/evaluator!
     *
     * See: https://git.lix.systems/lix-project/lix/issues/149
     */
    bool escapeNonPrinting = false;
};

/**
 * Escape a string for output.
 *
 * With default optional parameters, the output string will round-trip through
 * the Nix evaluator (i.e. you can copy/paste this function's output into the
 * REPL and have it evaluate as the string that got passed in).
 *
 * With non-default optional parameters, the output string will be
 * human-readable.
 *
 * See `EscapeStringOptions` for more details on customizing the output.
 */
std::ostream &
escapeString(std::ostream & output, std::string_view s, EscapeStringOptions options = {});

inline std::ostream & escapeString(std::ostream & output, const char * s)
{
    return escapeString(output, std::string_view(s));
}

inline std::ostream & escapeString(std::ostream & output, const std::string & s)
{
    return escapeString(output, std::string_view(s));
}

/**
 * Escape a string for output, writing the escaped result to a new string.
 */
std::string escapeString(std::string_view s, EscapeStringOptions options = {});

inline std::string escapeString(const char * s, EscapeStringOptions options = {})
{
    return escapeString(std::string_view(s), options);
}

} // namespace nix
