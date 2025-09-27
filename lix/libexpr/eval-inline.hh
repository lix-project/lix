#pragma once
///@file

#include "lix/libexpr/print.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/eval-error.hh"
#include "lix/libexpr/gc-alloc.hh"
#include "value.hh"
#include <cstdint>

namespace nix {

inline Value::Value(app_t, EvalMemory & mem, Value & lhs, Value & rhs)
{
    auto app = static_cast<Value::App *>(mem.allocBytes(sizeof(Value::App) + sizeof(Value *)));
    app->_left = lhs;
    app->_n = 1;
    app->_args[0] = rhs;
    raw = tag(tApp, app);
}

inline Value::Value(app_t, EvalMemory & mem, Value & lhs, std::span<Value> args)
{
    auto app = static_cast<Value::App *>(mem.allocBytes(sizeof(Value::App) + args.size_bytes()));
    app->_left = lhs;
    app->_n = args.size();
    std::copy(args.begin(), args.end(), app->_args);
    raw = tag(tApp, app);
}

inline Value::Value(thunk_t, EvalMemory & mem, Env & env, Expr & expr)
{
    auto thunk = mem.allocType<Thunk>();
    *thunk = {._env = &env, .expr = &expr};
    raw = tag(tThunk, thunk);
}

inline Value::Value(lambda_t, EvalMemory & mem, Env & env, ExprLambda & lambda)
{
    auto lp = mem.allocType<Lambda>();
    new (lp) Lambda{env, lambda};
    raw = tag(tAuxiliary, lp);
}

[[gnu::always_inline]]
void * EvalMemory::allocBytes(size_t size)
{
#if HAVE_BOEHMGC
    /* We use the boehm batch allocator to speed up allocations of Values (of which there are many).
       GC_malloc_many returns a linked list of objects of the given size, where the first word
       of each object is also the pointer to the next object in the list. This also means that we
       have to explicitly clear the first word of every object we take. */
    // NOTE: we purposely do not allocate 0 byte blocks on caches; we never allocate
    // zero bytes anyway, and it makes cache index calculation a little bit simpler.
    const auto cacheIdx = (size - 1) / CACHE_INCREMENT;
    if (cacheIdx < CACHES) {
        const auto roundedSize = (cacheIdx + 1) * CACHE_INCREMENT;
        auto & cache = gcCache[cacheIdx];
        if (!cache) {
            cache = GC_malloc_many(roundedSize);
            if (!cache) {
                throw std::bad_alloc();
            }
        }

        /* GC_NEXT is a convenience macro for accessing the first word of an object.
           Take the first list item, advance the list to the next item, and clear the next pointer.
         */
        void * p = cache;
        cache = GC_NEXT(p);
        GC_NEXT(p) = nullptr;
        return p;
    }
#endif

    return gcAllocBytes(size);
}

/// `gcAllocType`, but using allocation caches to amortize allocation overhead.
template<typename T>
[[gnu::always_inline]]
T * EvalMemory::allocType(size_t n)
{
    // NOLINTNEXTLINE(bugprone-sizeof-expression)
    return static_cast<T *>(allocBytes(checkedArrayAllocSize(sizeof(T), n)));
}

[[gnu::always_inline]]
Env & EvalMemory::allocEnv(size_t size)
{
    static_assert(CACHES * CACHE_INCREMENT >= sizeof(Env) + sizeof(Value *));

    stats.nrEnvs++;
    stats.nrValuesInEnvs += size;

    Env * env = static_cast<Env *>(allocBytes(sizeof(Env) + size * sizeof(Value *)));

    /* We assume that env->values has been cleared by the allocator; maybeThunk() and lookupVar fromWith expect this. */

    return *env;
}


[[gnu::always_inline]]
void EvalState::forceValue(Value & v, const PosIdx pos)
{
    if (v.isThunk()) {
        auto & thunk = v.thunk();
        if (thunk.resolved()) {
            v = thunk.result();
        } else {
            const auto backup = thunk;
            Env * env = thunk.env();
            Expr & expr = *thunk.expr;
            thunk = Value::blackHole;
            try {
                expr.eval(*this, *env, v);
                thunk.resolve(v);
            } catch (...) {
                thunk = backup;
                tryFixupBlackHolePos(v, pos);
                throw;
            }
        }
    } else if (v.isApp()) {
        auto & app = v.app();
        if (app.resolved()) {
            v = app.result();
        } else {
            auto target = app.target();
            if (!target.isPrimOp() || target.primOp()->arity <= app.totalArgs()) {
                auto tmp = v.app().left();
                callFunction(tmp, v.app().args(), v, pos);
                app.resolve(v);
            }
        }
    }
}

[[gnu::always_inline]]
inline void EvalState::forceAttrs(Value & v, const PosIdx pos, std::string_view errorCtx)
{
    forceValue(v, pos);
    if (v.type() != nAttrs) {
        ctx.errors.make<TypeError>(
            "expected a set but found %1%: %2%",
            showType(v),
            ValuePrinter(*this, v, errorPrintOptions)
        ).withTrace(pos, errorCtx).debugThrow();
    }
}


[[gnu::always_inline]]
inline void EvalState::forceList(Value & v, const PosIdx pos, std::string_view errorCtx)
{
    forceValue(v, pos);
    if (!v.isList()) {
        ctx.errors.make<TypeError>(
            "expected a list but found %1%: %2%",
            showType(v),
            ValuePrinter(*this, v, errorPrintOptions)
        ).withTrace(pos, errorCtx).debugThrow();
    }
}


}
