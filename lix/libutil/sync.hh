#pragma once
///@file

#include "lix/libutil/types.hh"
#include <cstdlib>
#include <kj/async.h>
#include <kj/common.h>
#include <list>
#include <mutex>
#include <condition_variable>
#include <cassert>
#include <optional>
#include <utility>

namespace nix {

struct AsyncMutex;

/**
 * This template class ensures synchronized access to a value of type
 * T. It is used as follows:
 *
 *   struct Data { int x; ... };
 *
 *   Sync<Data> data;
 *
 *   {
 *     auto data_(data.lock());
 *     data_->x = 123;
 *   }
 *
 * Here, "data" is automatically unlocked when "data_" goes out of
 * scope.
 */
template<class T, class M = std::mutex>
class Sync
{
private:
    M mutex;
    T data;

public:

    Sync() { }
    Sync(const T & data) : data(data) { }
    Sync(T && data) noexcept : data(std::move(data)) { }

    template<typename ... Args>
    Sync(std::in_place_t, Args &&... args) : data(std::forward<Args>(args)...) { }

    class Lock
    {
    protected:
        // Non-owning pointer. This would be an
        // optional<reference_wrapper<Sync>> if it didn't break gdb accessing
        // Lock values (as of 2024-06-15, gdb 14.2)
        Sync * s;
        std::unique_lock<M> lk;
        friend Sync;
        Lock(Sync &s) : s(&s), lk(s.mutex) { }
        Lock(Sync &s, std::unique_lock<M> lk) : s(&s), lk(std::move(lk)) { }

        inline void checkLockingInvariants()
        {
            assert(s);
            assert(lk.owns_lock());
        }

    public:
        Lock(Lock && l) : s(l.s), lk(std::move(l.lk))
        {
            l.s = nullptr;
        }

        Lock & operator=(Lock && other)
        {
            if (this != &other) {
                s = other.s;
                lk = std::move(other.lk);
                other.s = nullptr;
            }
            return *this;
        }

        Lock(const Lock & l) = delete;

        ~Lock() = default;

        T * operator -> ()
        {
            checkLockingInvariants();
            return &s->data;
        }

        T & operator * ()
        {
            checkLockingInvariants();
            return s->data;
        }

        /**
         * Wait for the given condition variable with no timeout.
         *
         * May spuriously wake up.
         */
        void wait(std::condition_variable & cv)
        {
            checkLockingInvariants();
            cv.wait(lk);
        }

        /**
         * Wait for the given condition variable for a maximum elapsed time of \p duration.
         *
         * May spuriously wake up.
         */
        template<class Rep, class Period>
        std::cv_status wait_for(std::condition_variable & cv,
            const std::chrono::duration<Rep, Period> & duration)
        {
            checkLockingInvariants();
            return cv.wait_for(lk, duration);
        }

        /**
         * Wait for the given condition variable for a maximum elapsed time of \p duration.
         * Calls \p pred to check if the wakeup should be heeded: \p pred
         * returning false will ignore the wakeup.
         */
        template<class Rep, class Period, class Predicate>
        bool wait_for(std::condition_variable & cv,
            const std::chrono::duration<Rep, Period> & duration,
            Predicate pred)
        {
            checkLockingInvariants();
            return cv.wait_for(lk, duration, pred);
        }

        /**
         * Wait for the given condition variable or until the time point \p duration.
         */
        template<class Clock, class Duration>
        std::cv_status wait_until(std::condition_variable & cv,
            const std::chrono::time_point<Clock, Duration> & duration)
        {
            checkLockingInvariants();
            return cv.wait_until(lk, duration);
        }
    };

    /**
     * Lock this Sync and return a RAII guard object.
     */
    Lock lock() { return Lock(*this); }

    std::optional<Lock> tryLock()
    {
        if (std::unique_lock lk(mutex, std::try_to_lock_t{}); lk.owns_lock()) {
            return Lock{*this, std::move(lk)};
        } else {
            return std::nullopt;
        }
    }
};

template<class T>
class Sync<T, AsyncMutex> : private Sync<T, std::mutex>
{
private:
    using base_type = Sync<T, std::mutex>;

    std::mutex waitMutex;
    // map of active waiters. contained fulfillers must still be waiting while waitMutex is
    // held, otherwise waking the first waiter in this map may fulfill a cancelled promise,
    // which in turn may starve the mutex if no further independent lock attempts are made.
    std::map<uint64_t, kj::Own<kj::CrossThreadPromiseFulfiller<void>>> waiters;
    // uint64 should be enough to never *ever* wrap. recall that 2**64 ns is over 500 years
    uint64_t waitSeq = 0;

    std::mutex conditionMutex;
    std::list<kj::Own<kj::CrossThreadPromiseFulfiller<void>>> conditionWaiters;

public:
    Sync() = default;
    Sync(T && data) : base_type(std::move(data)) {}

    class Lock : private base_type::Lock
    {
        friend Sync;

        Lock(base_type::Lock lk) : base_type::Lock(std::move(lk)) {}

    public:
        Lock(Lock &&) = default;
        Lock & operator=(Lock &&) = default;

        ~Lock()
        {
            if (this->lk.owns_lock()) {
                this->lk.unlock();
                auto * s = static_cast<Sync *>(this->s);
                std::lock_guard wlk(s->waitMutex);
                if (auto it = s->waiters.begin(); it != s->waiters.end()) {
                    it->second->fulfill();
                    s->waiters.erase(it);
                }
            }
        }

        using base_type::Lock::operator->, base_type::Lock::operator*;

        /**
         * Releases the lock, waits for another promise to call `Sync::notify`,
         * and reacquires the lock. There is no `condition_variable`-equivalent
         * object to allow multiple wait queues on the same lock since we don't
         * need that yet. There's no reason not to add such a type when needed.
         */
        kj::Promise<void> wait()
        {
            auto * s = static_cast<Sync *>(this->s);

            {
                auto unlock = std::move(*this);
            }

            auto pfp = kj::newPromiseAndCrossThreadFulfiller<void>();
            {
                std::lock_guard clk(s->conditionMutex);
                s->conditionWaiters.push_back(std::move(pfp.fulfiller));
            }
            co_await pfp.promise;

            *this = co_await s->lock();
        }
    };

    /**
     * Notify all promises awaiting `Lock::wait`. There is no `notify_one` like
     * `std::condition_variable` provides owing to implementation complexities.
     */
    void notify()
    {
        std::lock_guard clk(conditionMutex);
        for (auto & f : conditionWaiters) {
            f->fulfill();
        }
        conditionWaiters.clear();
    }

    auto lockSync(NeverAsync = {})
    {
        return base_type::lock();
    }

    kj::Promise<Lock> lock()
    {
        if (auto lk = tryLock()) {
            co_return std::move(*lk);
        }

        while (true) {
            auto pfp = kj::newPromiseAndCrossThreadFulfiller<void>();
            // enqueue this attempt as a waiter
            const auto seq = [&] {
                std::lock_guard wlk(waitMutex);
                auto seq = waitSeq++;
                waiters.emplace(seq, std::move(pfp.fulfiller));
                return seq;
            }();
            // unregister this waiter and signal the first remaining waiter if
            // this promise is cancelled before being granted the lock. we may
            // spuriously wake a waiter if exceptions occur without us holding
            // the lock, these waiters will then requeue themselves as needed.
            auto dequeueAndWake = kj::defer([&] {
                std::lock_guard wlk(waitMutex);
                waiters.erase(seq);
                if (auto it = waiters.begin(); it != waiters.end()) {
                    it->second->fulfill();
                    waiters.erase(it);
                }
            });
            if (auto lk = tryLock()) {
                std::lock_guard wlk(waitMutex);
                waiters.erase(seq);
                dequeueAndWake.cancel();
                co_return std::move(*lk);
            }
            co_await pfp.promise;
            dequeueAndWake.cancel();
        }
    }

    std::optional<Lock> tryLock()
    {
        if (auto lk = base_type::tryLock()) {
            return Lock(std::move(*lk));
        } else {
            return std::nullopt;
        }
    }
};

}
