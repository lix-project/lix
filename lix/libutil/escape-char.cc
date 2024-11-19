#include <boost/io/ios_state.hpp>
#include <iomanip>
#include <iostream>

#include "lix/libutil/escape-char.hh"

namespace nix {

std::ostream & operator<<(std::ostream & s, MaybeHexEscapedChar c)
{
    boost::io::ios_flags_saver _ifs(s);

    if (isprint(c.c)) {
        s << static_cast<char>(c.c);
    } else {
        s << "\\x" << std::hex << std::setfill('0') << std::setw(2)
          << (static_cast<unsigned int>(c.c) & 0xff);
    }
    return s;
}

} // namespace nix
