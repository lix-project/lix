#include "utils.hh"

#include "libutil/charptr-cast.hh"
#include "libutil/error.hh"
#include "libutil/fmt.hh"
#include "lix/lix-rs/main.gen.hh"
#include <cstdint>
#include <kj/async.h>
#include <kj/exception.h>

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

Ref<std::ffi::OsStr> to_os_str(::std::string_view sv)
{
    return lix::ffi::to_os_str(::nix::charptr_cast<const uint8_t *>(sv.begin()), sv.size());
}

std::collections::hash_set::HashSet<String> to_hash_set(const ::std::set<::std::string> & s)
{
    auto hs = std::collections::hash_set::HashSet<String>::new_();
    for (auto & str : s) {
        hs.insert(to_string(str));
    }
    return hs;
}

std::vec::Vec<String> to_vec(const ::std::vector<::std::string> & s)
{
    auto v = std::vec::Vec<String>::with_capacity(s.size());
    for (auto & str : s) {
        v.push(to_string(str));
    }
    return v;
}

std::vec::Vec<String> to_vec(const ::std::list<::std::string> & s)
{
    auto v = std::vec::Vec<String>::with_capacity(s.size());
    for (auto & str : s) {
        v.push(to_string(str));
    }
    return v;
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

void std::result::detail::throw_from_report(rootcause::Report & r)
{
    throw ::nix::Error("%s", ::nix::Uncolored(to_std_string(r.to_string())));
}

rootcause::Report lix::errors::current_exception_as_report()
{
    try {
        throw; // NOLINT(lix-foreign-exceptions)
    } catch (::std::exception & e) { // NOLINT(lix-foreign-exceptions)
        return errors::report_from_string_unhooked(to_string(e.what()));
    } catch (...) {
        return errors::report_from_string_unhooked(to_string("unknown exception type"));
    }
}

namespace lix::futures {
void Waker::wake() const noexcept
{
    try {
        executor->executeSync([this] { fulfiller->fulfill(); });
    } catch (kj::Exception & e) { // NOLINT(lix-foreign-exceptions)
        if (e.getType() == kj::Exception::Type::DISCONNECTED) {
            // ignore. the remote executor exited before out callback ran. this can happen if a
            // waker is used from another thread during shutdown of the executor it would wake.
        } else {
            ::std::terminate();
        }
    } catch (...) {
        ::std::terminate();
    }
}

void Waker::addRef() const noexcept
{
    refs++;
}

void Waker::dropRef() const noexcept
{
    try {
        if (refs.fetch_sub(1) == 1) {
            delete this;
        }
    } catch (...) {
        ::std::terminate();
    }
}
}

Raw<Unit> Impl<lix::futures::CxxWaker, Inherent>::clone(Raw<Unit> w)
{
    reinterpret_cast<lix::futures::Waker *>(w.addr())->addRef();
    return w;
}

Unit rust::Impl<lix::futures::CxxWaker, Inherent>::wake(Raw<Unit> w)
{
    return wake_by_ref(w);
}

Unit rust::Impl<lix::futures::CxxWaker, Inherent>::wake_by_ref(Raw<Unit> w)
{
    reinterpret_cast<lix::futures::Waker *>(w.addr())->wake();
    return Unit{};
}

Unit rust::Impl<lix::futures::CxxWaker, Inherent>::drop(Raw<Unit> w)
{
    reinterpret_cast<lix::futures::Waker *>(w.addr())->dropRef();
    return Unit{};
}
}
