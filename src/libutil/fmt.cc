#include "fmt.hh" // IWYU pragma: keep

template class boost::basic_format<char>;

namespace nix {

// Explicit instantiation saves about 30 cpu-seconds of compile time
template HintFmt::HintFmt(const std::string &, const Uncolored<std::string> &s);
template HintFmt::HintFmt(const std::string &, const std::string &s);
template HintFmt::HintFmt(const std::string &, const uint64_t &, const char * const &);

HintFmt::HintFmt(const std::string & literal) : HintFmt("%s", Uncolored(literal)) {}

}
