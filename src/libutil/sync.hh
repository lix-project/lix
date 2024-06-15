#pragma once
///@file

#include <cstdlib>
#include <mutex>
#include <condition_variable>
#include <cassert>

namespace nix {

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

    class Lock
    {
    private:
        // Non-owning pointer. This would be an
        // optional<reference_wrapper<Sync>> if it didn't break gdb accessing
        // Lock values (as of 2024-06-15, gdb 14.2)
        Sync * s;
        std::unique_lock<M> lk;
        friend Sync;
        Lock(Sync &s) : s(&s), lk(s.mutex) { }

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
};

}
