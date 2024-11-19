#pragma once
/// @file
/// @brief A semaphore implementation usable from within a KJ event loop.

#include <cassert>
#include <kj/async.h>
#include <kj/common.h>
#include <kj/exception.h>
#include <kj/list.h>
#include <kj/source-location.h>
#include <memory>
#include <optional>

namespace nix {

class AsyncSemaphore
{
public:
    class [[nodiscard("destroying a semaphore guard releases the semaphore immediately")]] Token
    {
        struct Release
        {
            void operator()(AsyncSemaphore * sem) const
            {
                sem->unsafeRelease();
            }
        };

        std::unique_ptr<AsyncSemaphore, Release> parent;

    public:
        Token() = default;
        Token(AsyncSemaphore & parent, kj::Badge<AsyncSemaphore>) : parent(&parent) {}

        bool valid() const
        {
            return parent != nullptr;
        }
    };

private:
    struct Waiter
    {
        kj::PromiseFulfiller<Token> & fulfiller;
        kj::ListLink<Waiter> link;
        kj::List<Waiter, &Waiter::link> & list;

        Waiter(kj::PromiseFulfiller<Token> & fulfiller, kj::List<Waiter, &Waiter::link> & list)
            : fulfiller(fulfiller)
            , list(list)
        {
            list.add(*this);
        }

        ~Waiter()
        {
            if (link.isLinked()) {
                list.remove(*this);
            }
        }
    };

    const unsigned capacity_;
    unsigned used_ = 0;
    kj::List<Waiter, &Waiter::link> waiters;

    void unsafeRelease()
    {
        used_ -= 1;
        while (used_ < capacity_ && !waiters.empty()) {
            used_ += 1;
            auto & w = waiters.front();
            w.fulfiller.fulfill(Token{*this, {}});
            waiters.remove(w);
        }
    }

public:
    explicit AsyncSemaphore(unsigned capacity) : capacity_(capacity) {}

    KJ_DISALLOW_COPY_AND_MOVE(AsyncSemaphore);

    ~AsyncSemaphore()
    {
        assert(waiters.empty() && "destroyed a semaphore with active waiters");
    }

    std::optional<Token> tryAcquire()
    {
        if (used_ < capacity_) {
            used_ += 1;
            return Token{*this, {}};
        } else {
            return {};
        }
    }

    kj::Promise<Token> acquire()
    {
        if (auto t = tryAcquire()) {
            return std::move(*t);
        } else {
            return kj::newAdaptedPromise<Token, Waiter>(waiters);
        }
    }

    unsigned capacity() const
    {
        return capacity_;
    }

    unsigned used() const
    {
        return used_;
    }

    unsigned available() const
    {
        return capacity_ - used_;
    }
};
}
