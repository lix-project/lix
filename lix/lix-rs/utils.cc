#include "utils.hh"

#include "libutil/charptr-cast.hh"
#include "lix/lix-rs/main.gen.hh"

namespace rust {
::std::string_view to_std_string_view(Ref<Str> s)
{
    return {nix::charptr_cast<const char *>(s.as_ptr()), s.len()};
}

::std::string_view std::string::to_std_string_view(const String & s)
{
    auto data = s.as_bytes();
    return {nix::charptr_cast<const char *>(data.as_ptr()), data.len()};
}

::std::string to_std_string(Ref<Str> s)
{
    return ::std::string(to_std_string_view(s));
}

::std::string std::string::to_std_string(const String & s)
{
    return ::std::string(to_std_string_view(s));
}

String to_string(::std::string_view sv)
{
    auto slice = lix::ffi::from_raw_parts_u8(nix::charptr_cast<const uint8_t *>(sv.begin()), sv.size());
    return String::from_utf8_lossy(slice).into_owned();
}

std::string::String Impl<lix::ffi::Error, Inherent>::to_string(Ref<lix::ffi::Error> ptr)
{
    using String = std::string::String;

    try {
        ::std::rethrow_exception(ptr.cpp());
    } catch (const ::std::exception & ex) { // NOLINT(lix-foreign-exceptions)
        auto slice =
            lix::ffi::from_raw_parts_u8(nix::charptr_cast<const uint8_t *>(ex.what()), strlen(ex.what()));
        return String::from_utf8_lossy(slice).into_owned();
    } catch (...) {
        return String::from("Unknown exception!"_rs);
    }
}
}
