#include "lix/libutil/print-elided.hh"
#include "lix/libutil/ansicolor.hh"
#include "lix/libutil/english.hh"

namespace nix {

void printElided(
    std::ostream & output,
    unsigned int value,
    const std::string_view single,
    const std::string_view plural,
    bool ansiColors)
{
    if (ansiColors)
        output << ANSI_FAINT;
    output << "«";
    pluralize(output, value, single, plural);
    output << " elided»";
    if (ansiColors)
        output << ANSI_NORMAL;
}

}
