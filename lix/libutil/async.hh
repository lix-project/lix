#pragma once
///@file

#include "lix/libutil/result.hh"
#include "lix/libutil/signals.hh"
#include <future>
#include <kj/async-io.h>
#include <kj/async.h>

namespace nix {

struct AsyncContext
{
    static inline thread_local AsyncContext * current = nullptr;

    kj::AsyncIoProvider & provider;
    kj::UnixEventPort & unixEventPort;

    explicit AsyncContext(kj::AsyncIoContext & aio)
        : provider(*aio.provider)
        , unixEventPort(aio.unixEventPort)
    {
        assert(current == nullptr);
        current = this;
    }

    ~AsyncContext()
    {
        current = nullptr;
    }

    KJ_DISALLOW_COPY_AND_MOVE(AsyncContext);
};

struct AsyncIoRoot
{
    kj::AsyncIoContext kj;
    AsyncContext context;

    AsyncIoRoot() : kj(kj::setupAsyncIo()), context(kj) {}
    KJ_DISALLOW_COPY_AND_MOVE(AsyncIoRoot);

    template<typename T>
    auto blockOn(kj::Promise<T> && promise);
};

inline AsyncContext & AIO()
{
    assert(AsyncContext::current != nullptr);
    return *AsyncContext::current;
}

namespace detail {
inline void materializeResult(Result<void> r)
{
    r.value();
}

template<typename T>
inline T materializeResult(Result<T> r)
{
    return std::move(r.value());
}

template<typename T>
T runAsyncUnwrap(T t)
{
    return t;
}
template<typename T>
T runAsyncUnwrap(Result<T> t)
{
    return std::move(t).value();
}

auto runAsyncInNewThread(std::invocable<AsyncIoRoot &> auto fn)
{
    auto future = std::async(std::launch::async, [&] {
        ReceiveInterrupts ri;
        AsyncIoRoot aioRoot;
        if constexpr (!std::is_void_v<decltype(fn(aioRoot))>) {
            return runAsyncUnwrap(fn(aioRoot));
        } else {
            fn(aioRoot);
        }
    });
    return future.get();
}
}
}

#define LIX_RUN_ASYNC_IN_NEW_THREAD(...)                            \
    ::nix::detail::runAsyncInNewThread([&](AsyncIoRoot & AIOROOT) { \
        return AIOROOT.blockOn(__VA_ARGS__);                        \
    })

// force materialization of the value. result::value() returns only an rvalue reference
// and is thus unsuitable for use in e.g. range for without materialization. ideally we
// would wrap the expression in `auto()`, but apple clang fails when given `auto(void)`
#define LIX_TRY_AWAIT(...) (::nix::detail::materializeResult(co_await (__VA_ARGS__)))

#if LIX_UR_COMPILER_UWU
# define RUN_ASYNC_IN_NEW_THREAD LIX_RUN_ASYNC_IN_NEW_THREAD
# define TRY_AWAIT LIX_TRY_AWAIT
#endif

template<typename T>
inline auto nix::AsyncIoRoot::blockOn(kj::Promise<T> && promise)
{
    return detail::runAsyncUnwrap(promise.wait(kj.waitScope));
}
