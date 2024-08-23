#include "fmt.hh" // IWYU pragma: keep
// Darwin and FreeBSD stdenv do not define _GNU_SOURCE but do have _Unwind_Backtrace.
#if __APPLE__ || __FreeBSD__
#define BOOST_STACKTRACE_GNU_SOURCE_NOT_REQUIRED
#endif
#include <boost/stacktrace/stacktrace.hpp>

template class boost::basic_format<char>;

namespace nix {

// Explicit instantiation saves about 30 cpu-seconds of compile time
template HintFmt::HintFmt(const std::string &, const Uncolored<std::string> &s);
template HintFmt::HintFmt(const std::string &, const std::string &s);
template HintFmt::HintFmt(const std::string &, const uint64_t &, const char * const &);

HintFmt::HintFmt(const std::string & literal) : HintFmt("%s", Uncolored(literal)) {}

void printStackTrace()
{
    std::cerr << boost::stacktrace::stacktrace() << std::endl;
}

}
