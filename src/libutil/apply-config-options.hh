#pragma once
/**
 * @file
 * @brief Options for applying `Config` settings.
 */

#include "lix/libutil/types.hh"

namespace nix {

/**
 * Options for applying `Config` settings.
 */
struct ApplyConfigOptions
{
    /**
     * The configuration file being loaded.
     *
     * If set, relative paths are allowed and interpreted as relative to the
     * directory of this path.
     */
    std::optional<Path> path = std::nullopt;

    /**
     * If set, tilde paths (like `~/.config/repl.nix`) are allowed and the
     * tilde is substituted for this directory.
     */
    std::optional<Path> home = std::nullopt;

    /**
     * Is the configuration being loaded from the `$NIX_CONFIG` environment
     * variable?
     *
     * Used for formatting error messages.
     */
    bool fromEnvVar = false;

    /**
     * Display the `relative` path field, with a reasonable default if none is
     * available.
     */
    std::string relativeDisplay() const {
        if (path) {
            return *path;
        } else if (fromEnvVar) {
            return "$NIX_CONFIG";
        } else {
            return "<unknown>";
        }
    }
};

}
