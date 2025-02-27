#pragma once
/// @file Manually destroy a value; suppresses automatic destruction
#include <atomic>
#include <cassert>
#include <utility>

namespace nix {

/** Analogous to Rust's ManuallyDrop structure. Only is destroyed when you call destroy(). */
template<typename T>
class ManuallyDrop
{
    alignas(alignof(T)) char data[sizeof(T)];
    // We have to use atomic<bool> here since once_flag reattempts if the
    // callee throws (double destruction? yikes)
    std::atomic<bool> destroyed = false;

public:
    explicit ManuallyDrop(T && t)
    {
        ::new (data) T(std::move(t));
    }

    /** Construct a ManuallyDrop in-place */
    template<typename... Arg>
    ManuallyDrop(std::in_place_t, Arg &&... args)
    {
        ::new (data) T(std::forward<Arg>(args)...);
    }

    ManuallyDrop(ManuallyDrop<T> && other)
    {
        ::new (data) ManuallyDrop<T>(other.take());
    }

    ~ManuallyDrop() {}

    // FIXME(jade): do some "deducing this" nonsense to implement all the const
    // whatevers for this class. my clangd didn't like it when i tried, so that
    // is Later Work. this language is horrific.

    /** Gets a reference to the inner T */
    T & get()
    {
        // SAFETY: this should genuinely never happen and it doesn't matter the
        // ordering of other stuff relative to it.
        assert(!destroyed.load(std::memory_order_relaxed));
        return reinterpret_cast<T &>(data);
    }

    T & operator*()
    {
        return get();
    }

    T * operator->()
    {
        return &get();
    }

    /**
     * Takes the value out of this object and gives it to you.
     * Must not already be destroyed.
     *
     * Example:
     ```c++
     ManuallyDrop<std::unique_ptr<int>> md{std::in_place_t{}, new int()};
     ManuallyDrop<std::unique_ptr<int>> md2{std::move(md).take()};
     ```
     */
    T && take() &&
    {
        // SAFETY: this is relatively lock-like in structure, reordering-wise
        bool wasDestroyed = destroyed.exchange(true, std::memory_order_acq_rel);
        assert(!wasDestroyed);
        return std::move(reinterpret_cast<T &>(data));
    }

    /** Destroy the value. Safe to call multiple times. */
    void destroy()
    {
        // SAFETY: this is relatively lock-like in structure, reordering-wise
        bool wasDestroyed = destroyed.exchange(true, std::memory_order_acq_rel);
        if (!wasDestroyed) {
            get().~T();
        }
    }
};
}
