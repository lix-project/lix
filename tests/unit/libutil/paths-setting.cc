#include "config.hh"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <sstream>

using testing::Eq;

namespace nix {

class PathsSettingTestConfig : public Config
{
public:
    PathsSettingTestConfig()
        : Config()
    { }

    PathsSetting paths{this, Paths(), "paths", "documentation"};
};

struct PathsSettingTest : public ::testing::Test {
public:
    PathsSettingTestConfig mkConfig()
    {
        return PathsSettingTestConfig();
    }
};

TEST_F(PathsSettingTest, parse) {
    auto config = mkConfig();
    // Not an absolute path:
    ASSERT_THROW(config.paths.parse("puppy.nix"), Error);

    ASSERT_THAT(
        config.paths.parse("/puppy.nix"),
        Eq<Paths>({"/puppy.nix"})
    );

    // Splits on whitespace:
    ASSERT_THAT(
        config.paths.parse("/puppy.nix /doggy.nix"),
        Eq<Paths>({"/puppy.nix", "/doggy.nix"})
    );

    // Splits on _any_ whitespace:
    ASSERT_THAT(
        config.paths.parse("/puppy.nix \t  /doggy.nix\n\n\n/borzoi.nix\r/goldie.nix"),
        Eq<Paths>({"/puppy.nix", "/doggy.nix", "/borzoi.nix", "/goldie.nix"})
    );

    // Canonicizes paths:
    ASSERT_THAT(
        config.paths.parse("/puppy/../doggy.nix"),
        Eq<Paths>({"/doggy.nix"})
    );
}

TEST_F(PathsSettingTest, bool) {
    auto config = mkConfig();
    // No paths:
    ASSERT_FALSE(config.paths);
    // Set a path:
    config.set("paths", "/puppy.nix");
    // Now there are paths:
    ASSERT_TRUE(config.paths);

    // Multiple paths count too:
    config.set("paths", "/puppy.nix /doggy.nix");
    ASSERT_TRUE(config.paths);
}

TEST_F(PathsSettingTest, append) {
    auto config = mkConfig();

    ASSERT_TRUE(config.paths.isAppendable());

    // Starts with no paths:
    ASSERT_THAT(
        config.paths.get(),
        Eq<Paths>({})
    );

    // Can append a path:
    config.paths.set("/puppy.nix", true);

    ASSERT_THAT(
        config.paths.get(),
        Eq<Paths>({"/puppy.nix"})
    );

    // Can append multiple paths:
    config.paths.set("/silly.nix /doggy.nix", true);

    ASSERT_THAT(
        config.paths.get(),
        Eq<Paths>({"/puppy.nix", "/silly.nix", "/doggy.nix"})
    );
}

} // namespace nix
