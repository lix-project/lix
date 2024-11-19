#pragma once
///@file

#include "lix/libutil/types.hh"
#include "lix/libutil/source-path.hh"

namespace nix {

/**
 * Helper function to generate args that invoke $EDITOR on
 * filename:lineno.
 */
Strings editorFor(const SourcePath & file, uint32_t line);

}
