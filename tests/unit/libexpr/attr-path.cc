#include "lix/libexpr/attr-path.hh"
#include "lix/libexpr/attr-set.hh"
#include "tests/libexpr.hh"
#include <gtest/gtest.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <rapidcheck/gen/Arbitrary.h>
#include <rapidcheck/gen/Container.h>
#include <rapidcheck/gen/Predicate.h>
#include <rapidcheck/gtest.h>
#pragma GCC diagnostic pop

namespace nix {

class AttrPathEval : public LibExprTest
{
public:
    std::pair<Value, PosIdx> testFindAlongAttrPath(std::string expr, std::string path);
};

RC_GTEST_PROP(AttrPath, prop_round_trip, ())
{
    auto strings = *rc::gen::container<std::vector<std::string>>(
        rc::gen::container<std::string>(rc::gen::distinctFrom('"'))
    );
    auto const unparsed = unparseAttrPath(strings);
    auto const unparsedReparsed = parseAttrPath(unparsed);

    RC_ASSERT(strings == unparsedReparsed);
}

std::pair<Value, PosIdx> AttrPathEval::testFindAlongAttrPath(std::string expr, std::string path)
{
    auto v = eval(expr);
    auto bindings = evalState().ctx.buildBindings(0).finish();
    return findAlongAttrPath(state, path, *bindings, v);
}

// n.b. I do not know why we throw for empty attrs but they are apparently
// disallowed.
TEST_F(AttrPathEval, emptyAttrsThrowsWithoutQuotes)
{
    std::string expr = "{a.\"\".b = 2;}";
    ASSERT_NO_THROW(testFindAlongAttrPath(expr, "a"));
    ASSERT_NO_THROW(testFindAlongAttrPath(expr, "a.\"\".b"));
    ASSERT_THROW(testFindAlongAttrPath(expr, "a..b"), Error);
    ASSERT_NO_THROW(testFindAlongAttrPath(expr, "a.\"\""));
}

TEST(attr_path_eval, quotes)
{
    auto p1 = parseAttrPath("foo.\"foo bar\".baz");
    ASSERT_EQ(3, p1.size());
    ASSERT_EQ("foo", p1[0]);
    ASSERT_EQ("foo bar", p1[1]);
    ASSERT_EQ("baz", p1[2]);

    auto p2 = parseAttrPath("foo.\"foo bar\"");
    ASSERT_EQ(2, p2.size());
    ASSERT_EQ("foo", p2[0]);
    ASSERT_EQ("foo bar", p2[1]);

    auto p3 = parseAttrPath("\"foo bar\"");
    ASSERT_EQ(1, p3.size());
    ASSERT_EQ("foo bar", p3[0]);
}

TEST(attr_path_eval, quotes_empty)
{
    auto p1 = parseAttrPath("foo.\"\".bar");
    ASSERT_EQ(3, p1.size());
    ASSERT_EQ("foo", p1[0]);
    ASSERT_EQ("", p1[1]);
    ASSERT_EQ("bar", p1[2]);

    auto p2 = parseAttrPath("foo.\"\"");
    ASSERT_EQ(2, p2.size());
    ASSERT_EQ("foo", p2[0]);
    ASSERT_EQ("", p2[1]);

    auto p3 = parseAttrPath("\"\"");
    ASSERT_EQ(1, p3.size());
    ASSERT_EQ("", p3[0]);
}

TEST(attr_path_eval, quotes_syntax)
{
    ASSERT_THROW(parseAttrPath("foo.\"bar"), ParseError);

    // escaped quotes (\") are not supported
    ASSERT_THROW(parseAttrPath("foo.\"bar\\\"\""), ParseError);
}
}
