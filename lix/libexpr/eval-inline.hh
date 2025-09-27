#pragma once
///@file

#include "lix/libexpr/print.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/eval-error.hh"
#include "lix/libexpr/gc-alloc.hh"
#include "value.hh"
#include <cstdint>

namespace nix {

inline Value::Value(app_t, EvalMemory & mem, Value & lhs, std::span<Value *> args)
    : internalType(tApp)
{
    if (args.size() == 1) {
        _app._left = reinterpret_cast<uintptr_t>(&lhs);
        _app._right = args[0];
    } else {
        auto app =
            static_cast<Value::AppN *>(mem.allocBytes(sizeof(Value::AppN) + args.size_bytes()));
        app->nargs = args.size();
        memcpy(app->args, args.data(), args.size_bytes());
        _app._left = reinterpret_cast<uintptr_t>(&lhs) | 1;
        _app._appn = app;
    }
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
Value * EvalMemory::allocValue()
{
    static_assert(CACHES * CACHE_INCREMENT >= sizeof(Value));
    stats.nrValues++;
    return static_cast<Value *>(allocBytes(sizeof(Value)));
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
        Env * env = v.thunk().env;
        Expr & expr = *v.thunk().expr;
        try {
            v.mkBlackhole();
            expr.eval(*this, *env, v);
        } catch (...) {
            v.mkThunk(env, expr);
            tryFixupBlackHolePos(v, pos);
            throw;
        }
    } else if (v.isApp()) {
        callFunction(*v.app().left(), v.app().args(), v, pos);
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
