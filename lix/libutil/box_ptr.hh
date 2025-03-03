#pragma once
/// @file

#include <concepts>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <assert.h>

namespace nix {

/** A pointer that's like Rust's Box: forwards comparisons to the inner class and is non-null */
template<typename T>
// FIXME: add custom deleter support
class box_ptr
{
    std::unique_ptr<T> inner;

    template<typename T2>
    friend class box_ptr;

    explicit box_ptr(std::unique_ptr<T> p)
        : inner(std::move(p))
    {
        assert(inner != nullptr);
    }

public:
    using pointer = typename std::unique_ptr<T>::pointer;

    inline typename std::add_lvalue_reference<T>::type operator*() const noexcept
    {
        return *inner.get();
    }

    inline pointer operator->() const noexcept
    {
        return inner.get();
    }

    inline pointer get() const noexcept
    {
        return inner.get();
    }

    /**
     * Create a box_ptr from a nonnull unique_ptr.
     */
    static inline box_ptr<T> unsafeFromNonnull(std::unique_ptr<T> p)
    {
        return box_ptr(std::move(p));
    }

    inline box_ptr<T> & operator=(box_ptr<T> && other) noexcept = default;

    // No copy for you.
    box_ptr<T> & operator=(const box_ptr<T> &) = delete;

    // XXX: we have to set the other's insides to nullptr, since we cannot
    // put a garbage object there, and we don't have any compiler
    // enforcement to not touch moved-from values. sighh.
    box_ptr(box_ptr<T> && other) = default;

    /** Conversion operator */
    template<typename Other>
    // n.b. the requirements here are the same as libstdc++ unique_ptr's checks but with concepts
    requires std::convertible_to<typename box_ptr<Other>::pointer, pointer> &&(!std::is_array_v<Other>)
        box_ptr(box_ptr<Other> && other) noexcept
        : inner(std::move(other.inner))
    {
        other.inner = nullptr;
    }

    box_ptr(box_ptr<T> & other) = delete;

    std::unique_ptr<T> take() &&
    {
        return std::move(inner);
    }
};

template<typename T>
requires std::equality_comparable<T>
bool operator==(box_ptr<T> const & x, box_ptr<T> const & y)
{
    // Although there could be an optimization here that compares x == y, this
    // is unsound for floats with NaN, or anything else that violates
    // reflexivity.
    return *x == *y;
}

template<typename T>
requires std::equality_comparable<T>
bool operator!=(box_ptr<T> const & x, box_ptr<T> const & y)
{
    return *x != *y;
}

#define MAKE_COMPARISON(OP) \
    template<typename T> \
    requires std::totally_ordered<T> \
    bool operator OP(box_ptr<T> const & x, box_ptr<T> const & y) \
    { \
        return *x OP * y; \
    }

MAKE_COMPARISON(<);
MAKE_COMPARISON(>);
MAKE_COMPARISON(>=);
MAKE_COMPARISON(<=);

#undef MAKE_COMPARISON

template<typename T>
requires std::three_way_comparable<T> std::compare_three_way_result_t<T, T>
operator<=>(box_ptr<T> const & x, box_ptr<T> const & y)
{
    return *x <=> *y;
}

template<typename T, typename... Args>
inline box_ptr<T> make_box_ptr(Args &&... args)
{
    return box_ptr<T>::unsafeFromNonnull(std::make_unique<T>(std::forward<Args>(args)...));
}
};
