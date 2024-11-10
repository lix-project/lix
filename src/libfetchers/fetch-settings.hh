#pragma once
///@file

#include "types.hh"
#include "config.hh"

#include <map>
#include <limits>

#include <sys/types.h>

namespace nix {

enum class AcceptFlakeConfig { False, Ask, True };

void to_json(nlohmann::json & j, const AcceptFlakeConfig & e);
void from_json(const nlohmann::json & j, AcceptFlakeConfig & e);

struct FetchSettings : public Config
{
    FetchSettings();

    #include "libfetchers-settings.gen.inc"
};

// FIXME: don't use a global variable.
extern FetchSettings fetchSettings;

}
