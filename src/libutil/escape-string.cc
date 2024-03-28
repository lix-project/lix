#include <iomanip>
#include <ostream>
#include <sstream>

#include "ansicolor.hh"
#include "escape-char.hh"
#include "english.hh"
#include "escape-string.hh"
#include "print-elided.hh"

namespace nix {

std::ostream &
escapeString(std::ostream & str, const std::string_view string, size_t maxLength, bool ansiColors)
{
    size_t charsPrinted = 0;
    if (ansiColors)
        str << ANSI_MAGENTA;
    str << "\"";
    for (auto i = string.begin(); i != string.end(); ++i) {
        if (charsPrinted >= maxLength) {
            str << "\" ";
            printElided(str, string.length() - charsPrinted, "byte", "bytes", ansiColors);
            return str;
        }
        if (*i == '\"' || *i == '\\') str << "\\" << *i;
        else if (*i == '\n') str << "\\n";
        else if (*i == '\r') str << "\\r";
        else if (*i == '\t') str << "\\t";
        else if (*i == '$' && *(i+1) == '{') str << "\\" << *i;
        else str << *i;
        charsPrinted++;
    }
    str << "\"";
    if (ansiColors)
        str << ANSI_NORMAL;
    return str;
}

}; // namespace nix
