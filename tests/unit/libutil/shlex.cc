#include "shlex.hh"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <sstream>

using testing::Eq;

namespace nix {

TEST(Shlex, shell_split) {
    ASSERT_THAT(shell_split(""), Eq<std::vector<std::string>>({}));
    ASSERT_THAT(shell_split("  "), Eq<std::vector<std::string>>({}));

    ASSERT_THAT(
        shell_split("puppy doggy"),
        Eq<std::vector<std::string>>({
            "puppy",
            "doggy",
        })
    );

    ASSERT_THAT(
        shell_split("goldie \"puppy 'doggy'\" sweety"),
        Eq<std::vector<std::string>>({
            "goldie",
            "puppy 'doggy'",
            "sweety",
        })
    );

    ASSERT_THAT(
        shell_split("\"pupp\\\"y\""),
        Eq<std::vector<std::string>>({ "pupp\"y" })
    );

    ASSERT_THAT(
        shell_split("goldie 'puppy' doggy"),
        Eq<std::vector<std::string>>({
            "goldie",
            "puppy",
            "doggy",
        })
    );

    ASSERT_THAT(
        shell_split("'pupp\\\"y'"),
        Eq<std::vector<std::string>>({
            "pupp\\\"y",
        })
    );

    ASSERT_THROW(shell_split("\"puppy"), ShlexError);
    ASSERT_THROW(shell_split("'puppy"), ShlexError);
}

} // namespace nix
