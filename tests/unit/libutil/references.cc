#include "lix/libutil/references.hh"
#include "lix/libutil/strings.hh"
#include <gtest/gtest.h>

namespace nix {

using std::string;

struct RewriteParams {
    string originalString, finalString;
    StringMap rewrites;

    friend std::ostream& operator<<(std::ostream& os, const RewriteParams& bar) {
        StringSet strRewrites;
        for (auto & [from, to] : bar.rewrites)
            strRewrites.insert(from + "->" + to);
        return os <<
            "OriginalString: " << bar.originalString << std::endl <<
            "Rewrites: " << concatStringsSep(",", strRewrites) << std::endl <<
            "Expected result: " << bar.finalString;
    }
};

class RewriteTest : public ::testing::TestWithParam<RewriteParams> {
};

TEST_P(RewriteTest, IdentityRewriteIsIdentity) {
    RewriteParams param = GetParam();
    StringSource src{param.originalString};
    ASSERT_EQ(RewritingSource(param.rewrites, src).drain(), param.finalString);
}

INSTANTIATE_TEST_CASE_P(
    references,
    RewriteTest,
    ::testing::Values(
        RewriteParams{ "foooo", "baroo", {{"foo", "bar"}, {"bar", "baz"}}},
        RewriteParams{ "foooo", "bazoo", {{"fou", "bar"}, {"foo", "baz"}}},
        RewriteParams{ "foooo", "foooo", {}},
        RewriteParams{ "babb", "bbbb", {{"ab", "aa"}, {"babb", "bbbb"}}}
    )
);

}
