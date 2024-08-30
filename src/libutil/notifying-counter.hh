#pragma once
/// @file

#include <cassert>
#include <functional>
#include <memory>

namespace nix {

template<std::integral T>
class NotifyingCounter
{
private:
    T counter;
    std::function<void()> notify;

public:
    class Bump
    {
        friend class NotifyingCounter;

        struct SubOnFree
        {
            T delta;

            void operator()(NotifyingCounter * c) const
            {
                c->add(-delta);
            }
        };

        // lightly misuse unique_ptr to get RAII types with destructor callbacks
        std::unique_ptr<NotifyingCounter<T>, SubOnFree> at;

        Bump(NotifyingCounter<T> & at, T delta) : at(&at, {delta}) {}

    public:
        Bump() = default;
        Bump(decltype(nullptr)) {}

        T delta() const
        {
            return at ? at.get_deleter().delta : 0;
        }

        void reset()
        {
            at.reset();
        }
    };

    explicit NotifyingCounter(std::function<void()> notify, T initial = 0)
        : counter(initial)
        , notify(std::move(notify))
    {
        assert(this->notify);
    }

    // bumps hold pointers to this, so we should neither copy nor move.
    NotifyingCounter(const NotifyingCounter &) = delete;
    NotifyingCounter & operator=(const NotifyingCounter &) = delete;
    NotifyingCounter(NotifyingCounter &&) = delete;
    NotifyingCounter & operator=(NotifyingCounter &&) = delete;

    T get() const
    {
        return counter;
    }

    operator T() const
    {
        return counter;
    }

    void add(T delta)
    {
        counter += delta;
        notify();
    }

    NotifyingCounter & operator+=(T delta)
    {
        add(delta);
        return *this;
    }

    NotifyingCounter & operator++(int)
    {
        return *this += 1;
    }

    Bump addTemporarily(T delta)
    {
        add(delta);
        return Bump{*this, delta};
    }
};

}
