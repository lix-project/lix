#pragma once
///@file

#include "lix/libutil/file-system.hh"

namespace nix {

/**
 * Create a temporary directory.
 */
Path createTempDir(const Path & tmpRoot = "", const Path & prefix = "nix",
    bool includePid = true, bool useGlobalCounter = true, mode_t mode = 0755);

/**
 * Create a temporary file, returning a file handle and its path.
 */
std::pair<AutoCloseFD, Path> createTempFile(const Path & prefix = "nix");

/**
 * Return settings.tempDir, `TMPDIR`, or the default temporary directory if unset or empty.
 */
Path defaultTempDir();

}
