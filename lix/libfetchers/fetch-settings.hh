#pragma once
///@file

#include "lix/libutil/types.hh"
#include "lix/libutil/config.hh"

#include <map>
#include <limits>

#include <sys/types.h>

namespace nix {

enum class AcceptFlakeConfig { False, Ask, True };

void to_json(JSON & j, const AcceptFlakeConfig & e);
void from_json(const JSON & j, AcceptFlakeConfig & e);

struct FetchSettings : public Config
{
    FetchSettings();

    #include "libfetchers-settings.gen.inc"
};

// FIXME: don't use a global variable.
extern FetchSettings fetchSettings;

}
