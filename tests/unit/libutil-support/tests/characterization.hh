#pragma once
///@file

#include "lix/libutil/environment-variables.hh"
#include "lix/libutil/file-system.hh"

#include <gtest/gtest.h>

#include <filesystem>

#include "lix/libutil/types.hh"
#include "test-data.hh"

namespace nix {

/**
 * Whether we should update "golden masters" instead of running tests
 * against them. See the contributing guide in the manual for further
 * details.
 */
static bool testAccept() {
    return getEnv("_NIX_TEST_ACCEPT") == "1";
}

/**
 * Mixin class for writing characterization tests
 */
class CharacterizationTest : public virtual ::testing::Test
{
protected:
    /**
     * While the "golden master" for this characterization test is
     * located. It should not be shared with any other test.
     */
    virtual Path goldenMaster(PathView testStem) const = 0;

public:
    /**
     * Golden test for reading
     *
     * @param test hook that takes the contents of the file and does the
     * actual work
     */
    void readTest(PathView testStem, auto && test)
    {
        auto file = goldenMaster(testStem);

        if (testAccept())
        {
            GTEST_SKIP()
                << "Cannot read golden master "
                << file
                << "because another test is also updating it";
        }
        else
        {
            test(readFile(file));
        }
    }

    /**
     * Golden test for writing
     *
     * @param test hook that produces contents of the file and does the
     * actual work
     */
    void writeTest(
        PathView testStem, auto && test, auto && readFile2, auto && writeFile2)
    {
        auto file = goldenMaster(testStem);

        auto actual = test();

        if (testAccept())
        {
            createDirs(dirOf(file));
            writeFile2(file, actual);
            GTEST_SKIP()
                << "Updating golden master "
                << file;
        }
        else
        {
            decltype(actual) expected = readFile2(file);
            ASSERT_EQ(expected, actual);
        }
    }

    /**
     * Specialize to `std::string`
     */
    void writeTest(PathView testStem, auto && test)
    {
        writeTest(
            testStem, test,
            [](const Path & f) -> std::string {
                return readFile(f);
            },
            [](const Path & f, const std::string & c) {
                return writeFile(f, c);
            });
    }
};

constexpr std::string_view cannotReadGoldenMaster =
    "Cannot read golden master because another test is also updating it";

constexpr std::string_view updatingGoldenMaster =
    "Updating golden master";

}
