#pragma once
///@file

#include "lix/libutil/types.hh"

#include <sys/types.h>

namespace nix {

std::string getUserName();

/**
 * @return the given user's home directory from /etc/passwd.
 */
Path getHomeOf(uid_t userId);

/**
 * @return $HOME or the user's home directory from /etc/passwd.
 */
Path getHome();

/**
 * @return $XDG_CACHE_HOME or $HOME/.cache.
 */
Path getCacheDir();

/**
 * @return $XDG_CONFIG_HOME or $HOME/.config.
 */
Path getConfigDir();

/**
 * @return the directories to search for user configuration files
 */
std::vector<Path> getConfigDirs();

/**
 * @return $XDG_DATA_HOME or $HOME/.local/share.
 */
Path getDataDir();

/**
 * @return $XDG_STATE_HOME or $HOME/.local/state.
 *
 * @note Not to be confused with settings.nixStateDir.
 */
Path getStateDir();

/**
 * Create $XDG_STATE_HOME/nix or $HOME/.local/state/nix, and return
 * the path to it.
 * @note Not to be confused with settings.nixStateDir.
 */
Path createNixStateDir();

/**
 * Perform tilde expansion on a path.
 */
std::string expandTilde(std::string_view path);

}
