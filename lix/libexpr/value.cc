#include "lix/libexpr/value.hh"

#include <ostream>

#include "lix/libexpr/eval.hh"
#include "lix/libexpr/print.hh"


namespace nix
{

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
    primop.check();
}


void Value::print(EvalState & state, std::ostream & str, PrintOptions options)
{
    printValue(state, str, *this, options);
}

PosIdx Value::determinePos(const PosIdx pos) const
{
    // Allow selecting a subset of enum values
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (internalType) {
        case tAttrs: return attrs->pos;
        case tLambda: return lambda.fun->pos;
        case tApp: return app.left->determinePos(pos);
        default: return pos;
    }
    #pragma GCC diagnostic pop
}

bool Value::isTrivial() const
{
    return
        internalType != tApp
        && internalType != tPrimOpApp
        && (internalType != tThunk
            || (dynamic_cast<ExprAttrs *>(thunk.expr)
                && static_cast<ExprAttrs *>(thunk.expr)->dynamicAttrs.empty())
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
    p->check();
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
    mkPath(gcCopyStringIfNeeded(path.path.abs()));
}

}
