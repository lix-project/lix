#pragma once

#include "types.hh"
#include "environment-variables.hh"
#include "file-system.hh"

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
