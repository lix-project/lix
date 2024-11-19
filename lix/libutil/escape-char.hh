#pragma once
///@file

#include <ostream>

namespace nix {

/**
 * A struct that prints a debug representation of a character, like `\x1f` for
 * non-printable characters, or the character itself for other characters.
 *
 * Note that these are suitable for human readable output, but further care is
 * necessary to include them in C++ strings to avoid running into adjacent
 * hex-like characters. (`"puppy\x1bdoggy"` parses as `"puppy" "\x1bd" "oggy"`
 * and errors because 0x1bd is too big for a `char`.)
 */
struct MaybeHexEscapedChar
{
    char c;
};

std::ostream & operator<<(std::ostream & s, MaybeHexEscapedChar c);

} // namespace nix
