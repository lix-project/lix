#include "lix/libexpr/value.hh"

#include <ostream>

#include "lix/libexpr/eval.hh"
#include "lix/libexpr/print.hh"


namespace nix
{

static const Value::List emptyListData{.size = 0};
Value Value::EMPTY_LIST{Value::list_t{}, &emptyListData};

const Value::Null Value::NULL_ACB = {{Value::Acb::tNull}};

static_assert(alignof(Value::String) >= Value::TAG_ALIGN);
static_assert(alignof(Bindings) >= Value::TAG_ALIGN);
static_assert(alignof(Value::List) >= Value::TAG_ALIGN);
static_assert(alignof(Value::Thunk) >= Value::TAG_ALIGN);
static_assert(alignof(Value::App) >= Value::TAG_ALIGN);

static void copyContextToValue(Value::String & s, const NixStringContext & context)
{
    if (!context.empty()) {
        size_t n = 0;
        s.context = gcAllocType<char const *>(context.size() + 1);
        for (auto & i : context)
            s.context[n++] = gcCopyStringIfNeeded(i.to_string());
        s.context[n] = 0;
    }
}

Value::Value(primop_t, PrimOp & primop) : raw(tag(tAuxiliary, &primop)) {}

void Value::print(EvalState & state, std::ostream & str, PrintOptions options)
{
    printValue(state, str, *this, options);
}

bool Value::isTrivial() const
{
    return internalType() != tApp
        && (internalType() != tThunk
            || (thunk().expr->try_cast<ExprSet>()
                && static_cast<ExprSet *>(thunk().expr)->dynamicAttrs.empty())
            || thunk().expr->try_cast<ExprLambda>() || thunk().expr->try_cast<ExprList>());
}

void Value::mkPrimOp(PrimOp * p)
{
    *this = {NewValueAs::primop, *p};
}

void Value::mkString(std::string_view s)
{
    mkString(gcCopyStringIfNeeded(s));
}

void Value::mkString(std::string_view s, const NixStringContext & context)
{
    mkString(s);
    copyContextToValue(*untag<String *>(), context);
}

void Value::mkStringMove(const char * s, const NixStringContext & context)
{
    mkString(s);
    copyContextToValue(*untag<String *>(), context);
}

void Value::mkPath(const SourcePath & path)
{
    *this = Value(NewValueAs::path, path);
}

}
