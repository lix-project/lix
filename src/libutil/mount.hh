#pragma once
///@file

#include "lix/libutil/types.hh"

#if __linux__
namespace nix {

/**
 * Bind-mount file or directory from `source` to `destination`.
 * If source does not exist this will fail unless `optional` is set
 */
void bindPath(const Path & source, const Path & target, bool optional = false);

}
#endif
