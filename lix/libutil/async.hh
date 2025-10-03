#pragma once
///@file

#include "lix/libutil/error.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/signals.hh"
#include <future>
#include <kj/async-io.h>
#include <kj/async.h>
#include <kj/time.h>
#include <optional>
#include <source_location>
#include <type_traits>

namespace nix {

struct AsyncContext
{
    static inline thread_local AsyncContext * current = nullptr;

    kj::AsyncIoProvider & provider;
    kj::LowLevelAsyncIoProvider & lowLevelProvider;
    kj::UnixEventPort & unixEventPort;

    explicit AsyncContext(kj::AsyncIoContext & aio)
        : provider(*aio.provider)
        , lowLevelProvider(*aio.lowLevelProvider)
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

    /**
     * Wrap a promise in a timeout. `Result<void>` promises are turned into
     * `Result<bool>` promises, where `true` means that the wrapped promise
     * ran to completion and `false` means it timed out. Other promises are
     * wrapped to return `Result<std::optional<T>>` and return `nullopt` if
     * the have time out or wrap their inner type as an optional otherwise.
     */
    template<typename T>
    auto timeoutAfter(kj::Duration timeout, kj::Promise<Result<T>> && p)
    {
        using RetT = std::conditional_t<std::is_void_v<T>, bool, std::optional<T>>;
        return p
            .then([](Result<T> r) -> Result<RetT> {
                if (r.has_value()) {
                    if constexpr (std::is_void_v<T>) {
                        return true;
                    } else {
                        return std::move(r.value());
                    }
                } else {
                    return r.error();
                }
            })
            .exclusiveJoin(provider.getTimer().afterDelay(timeout).then([]() -> Result<RetT> {
                return RetT{};
            }));
    }
};

struct AsyncIoRoot
{
    kj::AsyncIoContext kj;
    AsyncContext context;

    AsyncIoRoot() : kj(kj::setupAsyncIo()), context(kj) {}
    KJ_DISALLOW_COPY_AND_MOVE(AsyncIoRoot);

    template<typename T>
    auto blockOn(
        kj::Promise<T> && promise, std::source_location call_site = std::source_location::current()
    );
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
}
}

#define LIX_TRY_AWAIT_CONTEXT_MAP(_l_ctx, _l_map, ...)                         \
    ({                                                                         \
        auto _lix_awaited = (_l_map) (co_await (__VA_ARGS__));                 \
        if (_lix_awaited.has_error()) {                                        \
            try {                                                              \
                _lix_awaited.value();                                          \
            } catch (::nix::BaseException & e) {                               \
                e.addAsyncTrace(::std::source_location::current(), _l_ctx());  \
                throw;                                                         \
            } catch (...) {                                                    \
                auto fe = ::nix::ForeignException::wrapCurrent();              \
                fe.addAsyncTrace(::std::source_location::current(), _l_ctx()); \
                throw fe;                                                      \
            }                                                                  \
        }                                                                      \
        ::nix::detail::materializeResult(std::move(_lix_awaited));             \
    })

#define LIX_TRY_AWAIT_CONTEXT(_l_ctx, ...) \
    LIX_TRY_AWAIT_CONTEXT_MAP(_l_ctx, (std::identity{}), __VA_ARGS__)

/**
 * Magic name used by `LIX_TRY_AWAIT` to insert additional context into an
 * async trace frame. This name will be looked up in the local scope every
 * time a try-await expression encounters an exception and then called. As
 * such it can be a function, a member function name, or even a type name.
 */
static constexpr std::optional<std::string> lixAsyncTaskContext()
{
    return std::nullopt;
}

// force materialization of the value. result::value() returns only an rvalue reference
// and is thus unsuitable for use in e.g. range for without materialization. ideally we
// would wrap the expression in `auto()`, but apple clang fails when given `auto(void)`
#define LIX_TRY_AWAIT(...) LIX_TRY_AWAIT_CONTEXT(lixAsyncTaskContext, __VA_ARGS__)

#if LIX_UR_COMPILER_UWU
#define TRY_AWAIT LIX_TRY_AWAIT
#endif

template<typename T>
inline auto nix::AsyncIoRoot::blockOn(kj::Promise<T> && promise, std::source_location call_site)
try {
    // always check for user interrupts. since this is c++ we must always be prepared for
    // random exceptions out of literally nowhere, which is why RAII is such an important
    // idiom. interruptions are also exceptions, so all exception-safe (and for promises,
    // cancellation-safe) code is automatically interruption-safe. in this code base with
    // its very creative approach to exception usage all promises *must* be cancellation-
    // safe to not wreck system state constantly, so calling checkInterrupt is safe here.
    checkInterrupt();
    return detail::runAsyncUnwrap(promise.wait(kj.waitScope));
} catch (BaseException & e) {
    e.addAsyncTrace(call_site);
    throw;
} catch (...) {
    auto fe = ForeignException::wrapCurrent();
    fe.addAsyncTrace(call_site);
    throw fe;
}
