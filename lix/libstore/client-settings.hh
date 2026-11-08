#pragma once
//@file

#include "lix/libstore/store-api.hh"
#include "lix/libutil/json.hh"
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

    /**
     * Apply the extended options blob sent by a client in `setOptions`.
     *
     * `exact` replaces the whole override map (the ssh-ng protocol sends
     * an allow-listed subset there), while `clientPreferences` are added to
     * the preference map. Both keys are optional.
     */
    void applyExtendedOptions(const JSON & blob);
};

/**
 * Whether a configuration setting is safe to let an untrusted client
 * override for its own connection, cf. `ClientSettings::apply`.
 */
bool isClientPreference(std::string_view name);
}
