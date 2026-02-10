#include "tests/libexpr.hh"
namespace nix {
using namespace testing;

// Mostly mirrored from print.cc in their constructions.

struct TypeValuePrintingTests : LibExprTest
{
    void test(Value v, std::string_view expected)
    {
        ASSERT_EQ(showType(v), expected);
    }
};

TEST_F(TypeValuePrintingTests, tInt)
{
    Value vInt = {NewValueAs::integer, 10};
    test(vInt, "an integer");
}

TEST_F(TypeValuePrintingTests, tBool)
{
    Value vBool = {NewValueAs::boolean, true};
    test(vBool, "a Boolean");
}

TEST_F(TypeValuePrintingTests, tString)
{
    Value vString = {NewValueAs::string, "some-string"};
    test(vString, "a string");
}

TEST_F(TypeValuePrintingTests, tStringWithCtx)
{
    const char * context[] = {"some context", nullptr};
    Value vString = {NewValueAs::string, "some-string", context};
    test(vString, "a string with context");
}

TEST_F(TypeValuePrintingTests, tPath)
{
    Value vPath = {NewValueAs::path, "/foo"};
    test(vPath, "a path");
}

TEST_F(TypeValuePrintingTests, tNull)
{
    Value vNull = {NewValueAs::null};
    test(vNull, "null");
}

TEST_F(TypeValuePrintingTests, tAttrs)
{
    Value vOne = {NewValueAs::integer, 1};
    Value vTwo = {NewValueAs::integer, 2};

    BindingsBuilder builder = evaluator.buildBindings(10);
    builder.insert(evaluator.symbols.create("one"), vOne);
    builder.insert(evaluator.symbols.create("two"), vTwo);

    Value vAttrs = {NewValueAs::attrs, builder.finish()};

    test(vAttrs, "a set");
}

TEST_F(TypeValuePrintingTests, tList)
{
    Value vOne = {NewValueAs::integer, 1};
    Value vTwo = {NewValueAs::integer, 2};

    auto vList = evaluator.mem.newList(5);
    vList->elems[0] = vOne;
    vList->elems[1] = vTwo;
    vList->size = 3;

    test(Value(NewValueAs::list, vList), "a list");
}

TEST_F(TypeValuePrintingTests, vThunk)
{
    EvalMemory mem;
    Env env;
    ExprInt e(noPos, 0);
    Value vThunk{NewValueAs::thunk, mem, env, e};

    test(vThunk, "a thunk");
}

TEST_F(TypeValuePrintingTests, vApp)
{
    EvalMemory mem;
    Value vFn{NewValueAs::null};
    Value vApp{NewValueAs::app, mem, vFn, vFn};

    test(vApp, "a function application");
}

TEST_F(TypeValuePrintingTests, vLambda)
{
    EvalMemory mem;
    Env env{.up = nullptr, .values = {}};
    PosTable::Origin origin = evaluator.positions.addOrigin(std::monostate(), 1);
    auto posIdx = evaluator.positions.add(origin, 0);

    ExprLambda eLambda(posIdx, std::make_unique<AttrsPattern>(), std::make_unique<ExprInt>(noPos, 0));
    eLambda.pattern->name = createSymbol("a");

    Value vLambda{NewValueAs::lambda, mem, env, eLambda};

    test(vLambda, "a function");

    eLambda.setName(createSymbol("puppy"));

    test(vLambda, "a function");
}

TEST_F(TypeValuePrintingTests, vPrimOp)
{
    PrimOp primOp{{.name = "puppy"}};
    Value vPrimOp = {NewValueAs::primop, primOp};

    test(vPrimOp, "the built-in function 'puppy'");
}

TEST_F(TypeValuePrintingTests, vPrimOpApp)
{
    EvalMemory mem;
    PrimOp primOp{{.name = "puppy"}};
    Value vPrimOp = {NewValueAs::primop, primOp};
    Value vPrimOpApp{NewValueAs::app, mem, vPrimOp, vPrimOp};

    test(vPrimOpApp, "the partially applied built-in function 'puppy'");
}

TEST_F(TypeValuePrintingTests, vExternal)
{
    struct MyExternal : ExternalValueBase
    {
    public:
        std::string showType() const override
        {
            return "an external value from MyExternal";
        }
        std::string typeOf() const override
        {
            return "";
        }
        virtual std::ostream & print(std::ostream & str) const override
        {
            str << "external value";
            return str;
        }
    } myExternal;

    Value vExternal;
    vExternal.mkExternal(&myExternal);

    test(vExternal, "an external value from MyExternal");
}

TEST_F(TypeValuePrintingTests, vFloat)
{
    Value vFloat = {NewValueAs::floating, 2.0};

    test(vFloat, "a float");
}

TEST_F(TypeValuePrintingTests, vBlackhole)
{
    Value vBlackhole{NewValueAs::blackhole};
    test(vBlackhole, "a black hole");
}

}
