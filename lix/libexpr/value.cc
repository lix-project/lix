#include "lix/libexpr/value.hh"

#include <ostream>

#include "lix/libexpr/eval.hh"
#include "lix/libexpr/print.hh"


namespace nix
{

static const Value::List emptyListData{.size = 0};
Value Value::EMPTY_LIST{Value::list_t{}, &emptyListData};

static void copyContextToValue(Value & v, const NixStringContext & context)
{
    if (!context.empty()) {
        size_t n = 0;
        v._string.context = gcAllocType<char const *>(context.size() + 1);
        for (auto & i : context)
            v._string.context[n++] = gcCopyStringIfNeeded(i.to_string());
        v._string.context[n] = 0;
    }
}

Value::Value(primop_t, PrimOp & primop)
    : internalType(tPrimOp)
    , _primOp(&primop)
    , _primop_pad(0)
{
}


void Value::print(EvalState & state, std::ostream & str, PrintOptions options)
{
    printValue(state, str, *this, options);
}

bool Value::isTrivial() const
{
    return internalType != tApp
        && (internalType != tThunk
            || (thunk().expr->try_cast<ExprSet>()
                && static_cast<ExprSet *>(thunk().expr)->dynamicAttrs.empty())
            || thunk().expr->try_cast<ExprLambda>() || thunk().expr->try_cast<ExprList>());
}

void Value::mkPrimOp(PrimOp * p)
{
    clearValue();
    internalType = tPrimOp;
    _primOp = p;
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
