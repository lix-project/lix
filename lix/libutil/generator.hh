#pragma once
///@file

#include "lix/libutil/types.hh"

#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

namespace nix {

template<typename T, typename Transform>
struct Generator;

namespace _generator {

template<typename T>
struct promise_state;
template<typename T>
struct GeneratorBase;

struct finished {};

template<typename T>
struct link
{
    std::coroutine_handle<> handle{};
    promise_state<T> * state{};
};

struct failure
{
    std::exception_ptr e;
};

template<typename T>
struct promise_state
{
    // result of the most recent coroutine resumption: a nested
    // coroutine to drain, a value, an error, or our completion
    std::variant<link<T>, T, failure, finished> value{};
    // coroutine to resume when this one has finished. set when
    // one generator yields another, such that the entire chain
    // of parents always linearly points to the root generator.
    link<T> parent{};
};

template<typename T, typename Transform>
struct promise : promise_state<T>
{
    using transform_t = std::conditional_t<std::is_void_v<Transform>, std::identity, Transform>;

    transform_t convert;
    std::optional<GeneratorBase<T>> inner;

    // called by the compiler to convert the internal promise object
    // to the user-declared (function return) type of the coroutine.
    Generator<T, Transform> get_return_object()
    {
        auto h = std::coroutine_handle<promise>::from_promise(*this);
        return Generator<T, Transform>(GeneratorBase<T>(h, h.promise()));
    }
    std::suspend_always initial_suspend()
    {
        return {};
    }
    std::suspend_always final_suspend() noexcept
    {
        return {};
    }
    void unhandled_exception()
    {
        this->value = failure{std::current_exception()};
    }

    // `co_yield` handler for "simple" values, i.e. those that
    // are transformed directly to a T by the given transform.
    template<typename From>
        requires requires(transform_t t, From && f) {
            {
                t(std::forward<From>(f))
            } -> std::convertible_to<T>;
        }
    std::suspend_always yield_value(From && from)
    {
        this->value.template emplace<1>(convert(std::forward<From>(from)));
        return {};
    }

    // `co_yield` handler for "complex" values, i.e. those that
    // are transformed into another generator. we'll drain that
    // new generator completely before resuming the current one
    template<typename From>
        requires requires(transform_t t, From && f) {
            static_cast<Generator<T, void>>(t(std::forward<From>(f)));
        }
    std::suspend_always yield_value(From && from)
    {
        inner = static_cast<Generator<T, void>>(convert(std::forward<From>(from))).impl;
        this->value = inner->active;
        return {};
    }

    // handler for `co_return`, including the implicit `co_return`
    // at the end of a coroutine that does not have one explicitly
    void return_void()
    {
        this->value = finished{};
    }
};

template<typename T>
struct GeneratorBase
{
    template<typename, typename>
    friend struct Generator;
    template<typename, typename>
    friend struct promise;

    // NOTE coroutine handles are LiteralType, own a memory resource (that may
    // itself own unique resources), and are "typically TriviallyCopyable". we
    // need to take special care to wrap this into a less footgunny interface.
    GeneratorBase(GeneratorBase && other)
    {
        swap(other);
    }

    GeneratorBase & operator=(GeneratorBase && other)
    {
        GeneratorBase(std::move(other)).swap(*this);
        return *this;
    }

    ~GeneratorBase()
    {
        if (h) {
            h.destroy();
        }
    }

    std::optional<T> next()
    {
        // resume the currently active coroutine once. it can return either a
        // value, an exception, another generator to drain, or it can finish.
        // since c++ coroutines cannot directly return anything from resume()
        // we must communicate all results via `active->state.value` instead.
        while (active.handle) {
            active.handle.resume();
            auto & p = *active.state;
            // process the result. only one case sets this to a non-`nullopt`
            // value, all others leave it at `nullopt` to request more loops.
            auto result = std::visit(
                overloaded{
                    // when the current coroutine handle is done we'll try to
                    // resume its parent (if the current handle was retrieved
                    // from a `co_yield`ed generator) or finish the generator
                    // entirely because the root active.parent has no handle.
                    [&](finished) -> std::optional<T> {
                        active = p.parent;
                        return {};
                    },
                    // when the coroutine yields a generator we push the full
                    // inner stack onto our own stack and resume the top item
                    [&](link<T> & inner) -> std::optional<T> {
                        auto base = inner.state;
                        while (base->parent.handle) {
                            base = base->parent.state;
                        }
                        base->parent = active;
                        active = inner;
                        return {};
                    },
                    // values are simply returned to the caller, as received.
                    [&](T & value) -> std::optional<T> { return std::move(value); },
                    // exceptions must be rethrown. resuming again after this
                    // is not allowed because the top-most coroutine would be
                    // finished and we'd thus step back to its parent, but by
                    // doing so we might invalidate invariants of the parent.
                    // allowing the parent to catch exceptions of a child for
                    // `co_yield` exceptions specifically would introduce far
                    // too many problems to be worth the doing (since parents
                    // can neither know nor revert any yields of their child)
                    [&](failure & f) -> std::optional<T> {
                        active = {};
                        std::rethrow_exception(f.e);
                    },
                },
                p.value
            );
            if (result) {
                return result;
            }
        }

        return std::nullopt;
    }

protected:
    std::coroutine_handle<> h{};
    link<T> active{};

    GeneratorBase(std::coroutine_handle<> h, promise_state<T> & state)
        : h(h)
        , active(h, &state)
    {
    }

    void swap(GeneratorBase & other)
    {
        std::swap(h, other.h);
        std::swap(active, other.active);
    }
};

} // _generator

/// Coroutine-based iterator modeled loosely on Rust [`std::iter::Iterator`][iter]
/// interface. Like Rust's `Iterator` and unlike common C++ iterators, a Generator
/// returns `std::optional<T>` values from its next() function, but unlike both it
/// can also transform items produced within using a Transform function object the
/// Generator holds before returning them via next(). To allow generator nesting a
/// Transform may also return another Generator instance for any yielded value, in
/// this case the new Generator will temporarily take priority over the previously
/// running one and have its values returned until it is exhausted, then return to
/// the previous Generator. This mechanism may nest Generator to arbitrary depths.
///
/// \tparam T item type
/// \tparam Transform transform function object type, or `void` for no transform
///
/// [iter]: https://doc.rust-lang.org/stable/std/iter/trait.Iterator.html
template<typename T, typename Transform = void>
struct Generator
{
    template<typename, typename>
    friend struct _generator::promise;
    // erasing the Transform type requires all generator types with a non-erased
    // Transform to access the private constructor of the erased type, but sadly
    // we cannot resonably restrict this to "T, non-void" without much more code
    // or compiler warnings on some versions of clang, e.g. the one darwin uses.
    template<typename, typename>
    friend struct Generator;

    using promise_type = _generator::promise<T, Transform>;

    Generator(const Generator &) = delete;
    Generator & operator=(const Generator &) = delete;
    Generator(Generator &&) = default;
    Generator & operator=(Generator &&) = default;

    /// If the coroutine held by the Generator has not finished, runs it until it
    /// yields a value, throws any exception, or returns. If the coroutine yields
    /// a value this value is passed to a persistent instance of `Transform` that
    /// is held by the Generator, and the result of this call is returned. If the
    /// coroutine throws an exception, or the Transform throws an exception while
    /// processing an item, that exception is rethrown and the Generator will not
    /// return any more non-`std::nullopt` values from next(). Once the contained
    /// coroutine has completed or an exception has been thrown the Generator can
    /// no longer return any valid values, only `std::nullopt`. Exceptions thrown
    /// are thrown only once, further invocations of next() return `std::nullopt`.
    ///
    /// \returns `std::nullopt` if the coroutine has completed, or a value
    std::optional<T> next()
    {
        return impl.next();
    }

    /// Type-erases the `Transform`.
    ///
    /// \return a new Generator with the `Transform` type-erased
    Generator<T, void> decay() &&
    {
        return Generator<T, void>(std::move(impl));
    }

    /// \copydoc decay()
    operator Generator<T, void>() &&
    {
        return std::move(*this).decay();
    }

    class iterator
    {
        // operator== must be const, but we need to call parent->next() to
        // be able to check whether the sequence has ended. boldface sigh.
        mutable Generator * parent = nullptr;
        mutable std::shared_ptr<T> item = nullptr;

        void step() const
        {
            auto next = parent->next();
            if (!next) {
                parent = nullptr;
                item = nullptr;
            } else if (!item) {
                item = std::make_shared<T>(std::move(*next));
            } else {
                *item = std::move(*next);
            }
        }

        void initializeIfNecessary() const
        {
            if (parent && !item) {
                step();
            }
        }

    public:
        using iterator_category = std::input_iterator_tag;
        using difference_type = void;
        using value_type = T;
        using reference = T &;
        using pointer = T *;

        iterator() = default;
        explicit iterator(Generator & parent) : parent(&parent) {}

        T * operator->() { return &**this; }
        T & operator*()
        {
            initializeIfNecessary();
            return *item;
        }

        iterator & operator++()
        {
            initializeIfNecessary();
            if (parent) {
                step();
            }
            return *this;
        }

        void operator++(int) { ++*this; }

        bool operator==(const iterator & b) const
        {
            initializeIfNecessary();
            return parent == nullptr && b.parent == nullptr;
        }
    };

    iterator begin() { return iterator{*this}; }
    iterator end() { return iterator{}; }

    friend iterator begin(Generator & g)
    {
        return g.begin();
    }
    friend iterator end(Generator & g)
    {
        return g.end();
    }

private:
    _generator::GeneratorBase<T> impl;

    explicit Generator(_generator::GeneratorBase<T> b) : impl(std::move(b)) {}
};

}
