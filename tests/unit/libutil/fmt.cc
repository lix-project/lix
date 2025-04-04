#include "lix/libutil/fmt.hh"
#include "lix/libutil/ansicolor.hh"

#include <gtest/gtest.h>

namespace nix {

TEST(HintFmt, arg_count)
{
    // Single arg is treated as a literal string.
    ASSERT_EQ(HintFmt("%s").str(), "%s");

    // Other strings format as expected:
    ASSERT_EQ(HintFmt("%s", 1).str(), ANSI_MAGENTA "1" ANSI_NORMAL);
    ASSERT_EQ(HintFmt("%1%", "hello").str(), ANSI_MAGENTA "hello" ANSI_NORMAL);

    // Argument counts are detected at construction.
    ASSERT_DEATH(HintFmt("%s %s", 1), "HintFmt received incorrect");

    ASSERT_DEATH(HintFmt("%s", 1, 2), "HintFmt received incorrect");
}

}
