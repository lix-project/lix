#include <sstream>
#include <string_view>

#include <gtest/gtest.h>

#include "tests/libexpr.hh"

#include "lix/libexpr/nixexpr.hh"
#include "lix/libutil/ref.hh"

namespace nix
{

using namespace testing;
struct ExprPrintingTests : LibExprTest
{
    void test(Expr const & expr, std::string_view expected)
    {
        std::stringstream out;
        expr.show(evaluator.symbols, out);
        ASSERT_EQ(out.str(), expected);
    }
};

TEST_F(ExprPrintingTests, ExprInheritFrom)
{
    // ExprInheritFrom has its own show() impl.
    // If it uses its parent class's impl it will crash.
    auto inheritSource = make_ref<ExprVar>(evaluator.symbols.create("stdenv"));
    ExprInheritFrom const eInheritFrom(noPos, 0, inheritSource);
    test(eInheritFrom, "(/* expanded inherit (expr) */ stdenv)");
}

}
