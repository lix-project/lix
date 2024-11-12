#pragma once
///@file
#include "lix/libutil/config.hh"

namespace nix {

struct EvalSettings : Config
{
    EvalSettings();

    static Strings getDefaultNixPath();

    static bool isPseudoUrl(std::string_view s);

    static std::string resolvePseudoUrl(std::string_view url);

    #include "libexpr-settings.gen.inc"

    /**
     * Implements the `eval-system` vs `system` defaulting logic
     * described for `eval-system`.
     */
    const std::string & getCurrentSystem();
};

extern EvalSettings evalSettings;

/**
 * Conventionally part of the default nix path in impure mode.
 */
Path getNixDefExpr();

}
