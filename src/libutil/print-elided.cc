#include "print-elided.hh"
#include "ansicolor.hh"
#include "english.hh"

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
