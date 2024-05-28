#pragma once

#include "environment-variables.hh"
#include "types.hh"

namespace nix {

// TODO: These helpers should be available in all unit tests.

/**
 * The path to the unit test data directory. See the contributing guide
 * in the manual for further details.
 */
static Path getUnitTestData() {
    return getEnv("_NIX_TEST_UNIT_DATA").value();
}

/**
 * Resolve a path under the unit test data directory to an absolute path.
 */
static Path getUnitTestDataPath(std::string_view path) {
    return absPath(getUnitTestData() + "/" + path);
}

}
