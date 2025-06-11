#pragma once
///@file

#include <functional>
#include <kj/async.h>
#include <limits>
#include <list>
#include <memory>
#include <cassert>
#include <optional>
#include <vector>

#include "lix/libutil/async.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/sync.hh"
#include "lix/libutil/ref.hh"
#include "lix/libutil/types.hh"

namespace nix {

/**
 * This template class implements a simple pool manager of resources
 * of some type R, such as database connections. It is used as
 * follows:
 *
 *   class Connection { ... };
 *
 *   Pool<Connection> pool;
 *
 *   {
 *     auto conn(pool.get());
 *     conn->exec("select ...");
 *   }
 *
 * Here, the Connection object referenced by ‘conn’ is automatically
 * returned to the pool when ‘conn’ goes out of scope.
 */
template <class R>
class Pool
{
public:

    /**
     * A function that produces new instances of R on demand.
     */
    typedef std::function<kj::Promise<Result<ref<R>>>()> Factory;

    /**
     * A function that checks whether an instance of R is still
     * usable. Unusable instances are removed from the pool.
     */
    typedef std::function<bool(const ref<R> &)> Validator;

private:

    Factory factory;
    Validator validator;

    struct State
    {
        size_t inUse = 0;
        size_t max;
        std::vector<ref<R>> idle;
        std::list<kj::Own<kj::CrossThreadPromiseFulfiller<void>>> waiters;

        void notify()
        {
            for (auto & waiter : waiters) {
                waiter->fulfill();
            }
            waiters.clear();
        }
    };

    Sync<State> state;

public:

    Pool(
        size_t max = std::numeric_limits<size_t>::max(),
        const Factory & factory = []() -> kj::Promise<Result<ref<R>>> {
            try {
                return {result::success(make_ref<R>())};
            } catch (...) {
                return {result::current_exception()};
            }
        },
        const Validator & validator = [](ref<R> r) { return true; }
    )
        : factory(factory)
        , validator(validator)
    {
        auto state_(state.lock());
        state_->max = max;
    }

    void incCapacity()
    {
        auto state_(state.lock());
        state_->max++;
        /* we could wakeup here, but this is only used when we're
         * about to nest Pool usages, and we want to save the slot for
         * the nested use if we can
         */
    }

    void decCapacity()
    {
        auto state_(state.lock());
        state_->max--;
    }

    ~Pool()
    {
        auto state_(state.lock());
        assert(!state_->inUse);
        state_->max = 0;
        state_->idle.clear();
    }

    class Handle
    {
    private:
        Pool & pool;
        std::shared_ptr<R> r;
        bool bad = false;

        friend Pool;

        Handle(Pool & pool, std::shared_ptr<R> r) : pool(pool), r(r) { }

    public:
        Handle(Handle && h) : pool(h.pool), r(h.r) { h.r.reset(); }

        Handle(const Handle & l) = delete;

        ~Handle()
        {
            if (!r) return;
            {
                auto state_(pool.state.lock());
                if (!bad)
                    state_->idle.push_back(ref<R>::unsafeFromPtr(r));
                assert(state_->inUse);
                state_->inUse--;
                state_->notify();
            }
        }

        R * operator -> () { return &*r; }
        R & operator * () { return *r; }

        void markBad() { bad = true; }
    };

private:
    void getFailed()
    {
        auto state_(state.lock());
        state_->inUse--;
        state_->notify();
    }

    // lock lifetimes must always be short, and *NEVER* cross a yield point.
    // we ensure this by using explicit continuations instead of coroutines.
    kj::Promise<Result<std::optional<Handle>>> tryGet()
    try {
        auto state_(state.lock());

        /* If we're over the maximum number of instance, we need
           to wait until a slot becomes available. */
        if (state_->idle.empty() && state_->inUse >= state_->max) {
            auto pfp = kj::newPromiseAndCrossThreadFulfiller<void>();
            state_->waiters.push_back(std::move(pfp.fulfiller));
            return pfp.promise.then([this] { return tryGet(); });
        }

        while (!state_->idle.empty()) {
            auto p = state_->idle.back();
            state_->idle.pop_back();
            if (validator(p)) {
                state_->inUse++;
                return {Handle(*this, p)};
            }
        }

        state_->inUse++;
        return {std::nullopt};
    } catch (...) {
        return {result::current_exception()};
    }

public:
    kj::Promise<Result<Handle>> get()
    try {
        if (auto existing = LIX_TRY_AWAIT(tryGet())) {
            co_return std::move(*existing);
        }

        /* We need to create a new instance. Because that might take a
           while, we don't hold the lock in the meantime. */
        try {
            Handle h(*this, LIX_TRY_AWAIT(factory()));
            co_return h;
        } catch (...) {
            getFailed();
            throw;
        }
    } catch (...) {
        co_return result::current_exception();
    }

    size_t count()
    {
        auto state_(state.lock());
        return state_->idle.size() + state_->inUse;
    }

    size_t capacity()
    {
        return state.lock()->max;
    }
};

}
