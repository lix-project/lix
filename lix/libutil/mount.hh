#pragma once
///@file

#include "lix/libutil/types.hh"
#include "lix/libutil/file-system.hh"

#if __linux__
namespace nix {

/**
 * Bind-mount file or directory from `source` to `destination`.
 * If source does not exist this will fail unless `optional` is set
 *
 * If `source` is a symlink, it will perform a copy instead of a bind mount
 * because symlinks cannot be bind mounted on all versions of the Linux kernel.
 *
 * If a copy is performed, extra flags to the copy can be passed using `flags`.
 */
void bindPath(
    const Path & source, const Path & target, bool optional = false, CopyFileFlags flags = {}
);
}
#endif
