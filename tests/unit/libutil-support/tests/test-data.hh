#pragma once
/// @file

#include "lix/libutil/types.hh"
#include "lix/libutil/environment-variables.hh"
#include "lix/libutil/file-system.hh"

namespace nix {

/**
 * The path to the unit test data directory. See the contributing guide
 * in the manual for further details.
 */
Path getUnitTestData();

/**
 * Resolve a path under the unit test data directory to an absolute path.
 */
Path getUnitTestDataPath(std::string_view path);

}
