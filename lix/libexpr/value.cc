#include "lix/libexpr/value.hh"

#include <ostream>

#include "lix/libexpr/eval.hh"
#include "lix/libexpr/print.hh"


namespace nix
{

Value Value::EMPTY_LIST{Value::list_t{}, {}};

static void copyContextToValue(Value & v, const NixStringContext & context)
{
    if (!context.empty()) {
        size_t n = 0;
        v.string.context = gcAllocType<char const *>(context.size() + 1);
        for (auto & i : context)
            v.string.context[n++] = gcCopyStringIfNeeded(i.to_string());
        v.string.context[n] = 0;
    }
}

Value::Value(primop_t, PrimOp & primop)
    : internalType(tPrimOp)
    , primOp(&primop)
    , _primop_pad(0)
{
}


void Value::print(EvalState & state, std::ostream & str, PrintOptions options)
{
    printValue(state, str, *this, options);
}

bool Value::isTrivial() const
{
    return
        internalType != tApp
        && internalType != tPrimOpApp
        && (internalType != tThunk
            || (dynamic_cast<ExprSet *>(thunk.expr)
                && static_cast<ExprSet *>(thunk.expr)->dynamicAttrs.empty())
            || dynamic_cast<ExprLambda *>(thunk.expr)
            || dynamic_cast<ExprList *>(thunk.expr));
}

PrimOp * Value::primOpAppPrimOp() const
{
    Value * left = primOpApp.left;
    while (left && !left->isPrimOp()) {
        left = left->primOpApp.left;
    }

    if (!left)
        return nullptr;
    return left->primOp;
}

void Value::mkPrimOp(PrimOp * p)
{
    clearValue();
    internalType = tPrimOp;
    primOp = p;
}

void Value::mkString(std::string_view s)
{
    mkString(gcCopyStringIfNeeded(s));
}

void Value::mkString(std::string_view s, const NixStringContext & context)
{
    mkString(s);
    copyContextToValue(*this, context);
}

void Value::mkStringMove(const char * s, const NixStringContext & context)
{
    mkString(s);
    copyContextToValue(*this, context);
}


void Value::mkPath(const SourcePath & path)
{
    *this = Value(NewValueAs::path, path);
}

}
