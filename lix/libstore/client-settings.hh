#pragma once
//@file

#include "lix/libstore/store-api.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/types.hh"

#include <string_view>

namespace nix {

struct ClientSettings
{
    Verbosity verbosity = lvlError;
    StringMap overrides;
    StringMap clientPreferences;

    void apply(TrustedFlag trusted);
};

/**
 * Whether a configuration setting is safe to let an untrusted client
 * override for its own connection, cf. `ClientSettings::apply`.
 */
bool isClientPreference(std::string_view name);
}
