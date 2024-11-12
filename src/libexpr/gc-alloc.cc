#include "lix/libexpr/gc-alloc.hh"

#include <cstring>
#include <string_view>

namespace nix
{

char const * gcCopyStringIfNeeded(std::string_view toCopyFrom)
{
    if (toCopyFrom.empty()) {
        return "";
    }

    size_t const size = toCopyFrom.size();
    char * cstr = gcAllocString(size + 1);
    memcpy(cstr, toCopyFrom.data(), size);
    cstr[size] = '\0';

    return cstr;
}

}
