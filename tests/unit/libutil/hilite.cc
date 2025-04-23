#include "lix/libutil/hilite.hh"
#include "lix/libutil/regex.hh"

#include <gtest/gtest.h>

namespace nix {
/* ----------- tests for fmt.hh -------------------------------------------------*/

    TEST(hiliteMatches, noHighlight) {
        ASSERT_STREQ(hiliteMatches("Hello, world!", std::vector<std::smatch>(), "(", ")").c_str(), "Hello, world!");
    }

    TEST(hiliteMatches, simpleHighlight) {
        std::string str = "Hello, world!";
        std::regex re = regex::parse("world");
        auto matches = std::vector(std::sregex_iterator(str.begin(), str.end(), re), std::sregex_iterator());
        ASSERT_STREQ(
                    hiliteMatches(str, matches, "(", ")").c_str(),
                    "Hello, (world)!"
        );
    }

    TEST(hiliteMatches, multipleMatches) {
        std::string str = "Hello, world, world, world, world, world, world, Hello!";
        std::regex re = regex::parse("world");
        auto matches = std::vector(std::sregex_iterator(str.begin(), str.end(), re), std::sregex_iterator());
        ASSERT_STREQ(
                    hiliteMatches(str, matches, "(", ")").c_str(),
                    "Hello, (world), (world), (world), (world), (world), (world), Hello!"
        );
    }

    TEST(hiliteMatches, overlappingMatches) {
        std::string str = "world, Hello, world, Hello, world, Hello, world, Hello, world!";
        std::regex re = regex::parse("Hello, world");
        std::regex re2 = regex::parse("world, Hello");
        auto v = std::vector(std::sregex_iterator(str.begin(), str.end(), re), std::sregex_iterator());
        for(auto it = std::sregex_iterator(str.begin(), str.end(), re2); it != std::sregex_iterator(); ++it) {
            v.push_back(*it);
        }
        ASSERT_STREQ(
                    hiliteMatches(str, v, "(", ")").c_str(),
                    "(world, Hello, world, Hello, world, Hello, world, Hello, world)!"
        );
    }

    TEST(hiliteMatches, complexOverlappingMatches) {
        std::string str = "legacyPackages.x86_64-linux.git-crypt";
        std::vector regexes = {
            regex::parse("t-cry"),
            regex::parse("ux\\.git-cry"),
            regex::parse("git-c"),
            regex::parse("pt"),
        };
        std::vector<std::smatch> matches;
        for(auto regex : regexes)
        {
            for(auto it = std::sregex_iterator(str.begin(), str.end(), regex); it != std::sregex_iterator(); ++it) {
                matches.push_back(*it);
            }
        }
        ASSERT_STREQ(
                    hiliteMatches(str, matches, "(", ")").c_str(),
                    "legacyPackages.x86_64-lin(ux.git-crypt)"
        );
    }
}
