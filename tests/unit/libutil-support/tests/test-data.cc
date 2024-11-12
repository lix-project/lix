#include "test-data.hh"
#include "lix/libutil/strings.hh"

namespace nix {

Path getUnitTestData()
{
    return getEnv("_NIX_TEST_UNIT_DATA").value();
}

Path getUnitTestDataPath(std::string_view path)
{
    return absPath(getUnitTestData() + "/" + path);
}

}
