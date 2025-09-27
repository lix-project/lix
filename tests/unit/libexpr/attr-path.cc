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
TEST_F(AttrPathEval, emptyAttrsThrows)
{
    std::string expr = "{a.\"\".b = 2;}";
    ASSERT_NO_THROW(testFindAlongAttrPath(expr, "a"));
    ASSERT_THROW(testFindAlongAttrPath(expr, "a.\"\".b"), Error);
    ASSERT_THROW(testFindAlongAttrPath(expr, "a.\"\""), Error);
}

}
