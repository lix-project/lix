#pragma once
///@file

#include <ostream>


namespace nix {

/**
 * Print an `«... elided»` placeholder.
 *
 * Arguments are forwarded to `pluralize`.
 *
 * If `ansiColors` is set, the output will be wrapped in `ANSI_FAINT`.
 */
void printElided(
    std::ostream & output,
    unsigned int value,
    const std::string_view single,
    const std::string_view plural,
    bool ansiColors
);

}
