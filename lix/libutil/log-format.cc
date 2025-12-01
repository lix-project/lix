#include "lix/libutil/log-format.hh"

static_assert(std::formattable<nix::LogFormat, char>);
static_assert(std::formattable<nix::LogFormatValue, char>);
