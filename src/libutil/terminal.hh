#pragma once
///@file

#include <limits>
#include <string>

namespace nix {

/**
 * Determine whether ANSI escape sequences are appropriate for the
 * present output.
 *
 * This follows the rules described on https://bixense.com/clicolors/
 * with CLICOLOR defaulted to enabled (and thus ignored).
 *
 * That is to say, the following procedure is followed in order:
 * - NO_COLOR or NOCOLOR set             -> always disable colour
 * - CLICOLOR_FORCE or FORCE_COLOR set   -> enable colour
 * - The output is a tty; TERM != "dumb" -> enable colour
 * - Otherwise                           -> disable colour
 */
bool shouldANSI();

/**
 * Truncate a string to 'width' printable characters. If 'filterAll'
 * is true, all ANSI escape sequences are filtered out. Otherwise,
 * some escape sequences (such as colour setting) are copied but not
 * included in the character count. Also, tabs are expanded to
 * spaces.
 */
std::string filterANSIEscapes(std::string_view s,
    bool filterAll = false,
    unsigned int width = std::numeric_limits<unsigned int>::max(),
    bool eatTabs = true);

/**
 * Recalculate the window size, updating a global variable. Used in the
 * `SIGWINCH` signal handler.
 */
void updateWindowSize();

/**
 * @return the number of rows and columns of the terminal.
 *
 * The value is cached so this is quick. The cached result is computed
 * by `updateWindowSize()`.
 */
std::pair<unsigned short, unsigned short> getWindowSize();

}
