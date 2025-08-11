#pragma once
///@file Helpers for processing legacy wire protocol data on async streams

#include "async-io.hh"
#include "result.hh"
#include <kj/async.h>
#include <type_traits>
#include <utility>

// #define HAVE_THREADBARE_LIBC 1

#if defined(HAVE_THREADBARE_LIBC)
#include "async.hh"
#include "thread-pool.hh"
#endif

namespace nix {
// Source wrappers for async streams. we must do this because the async deserialization overhead is
// too large otherwise; every await or blockOn consumes far more time than the actual copy/decoding
// done by the deserializer. this is especially important for buffered input streams since they can
// support many small wire protocol reads on a single syscall, making the async scheduling overhead
// even more of a loss compared to the old synchronous code. this will at least get us pretty close
namespace detail {
#if !defined(HAVE_THREADBARE_LIBC)
struct BufferedAsyncSource : Source
{
    kj::WaitScope & ws;
    AsyncBufferedInputStream & from;

    BufferedAsyncSource(kj::WaitScope & ws, AsyncBufferedInputStream & from) : ws(ws), from(from) {}

    size_t read(char * data, size_t len) override;
};

// stacks for wrappers. the wrapper sources need wait scopes to work, and those
// we can only get from fibers or running at the top level of an async tree. we
// can do the latter in the daemon, but remote stores also need to deserialize.
inline thread_local kj::FiberPool serializerFibers{65536};
#else
struct IndirectSource : Source
{
    const kj::Executor & executor;
    AsyncBufferedInputStream & from;

    IndirectSource(const kj::Executor & executor, AsyncBufferedInputStream & from)
        : executor(executor)
        , from(from)
    {
    }

    size_t read(char * data, size_t len) override;
};

extern ThreadPool deserPool;
#endif
}

/**
 * Wrap the async input stream `from` in a synchronous Source and run `fn` with
 * the wrapper as an argument, asynchronously, as a kj fiber. `fn` does not run
 * on the main stack and instead has only 64 kiB of stack space available. `fn`
 * should never block since only reading data from the wrapper source can yield
 * the executor to other promises. Use async deserializers instead if possible;
 * use this wrapper only to avoid async deserialization overhead when it hurts.
 */
inline auto deserializeFrom(AsyncBufferedInputStream & from, auto fn)
    -> kj::Promise<Result<decltype(fn(std::declval<Source &>()))>>
{
    using ResultT = decltype(fn(std::declval<Source &>()));

#if !defined(HAVE_THREADBARE_LIBC)
    return detail::serializerFibers.startFiber(
        [&from, fn{std::move(fn)}](kj::WaitScope & ws) -> Result<ResultT> {
            try {
                detail::BufferedAsyncSource wrapped{ws, from};
                if constexpr (std::is_void_v<ResultT>) {
                    fn(wrapped);
                    return result::success();
                } else {
                    return fn(wrapped);
                }
            } catch (...) {
                return result::current_exception();
            }
        }
    );
#else
    try {
        auto pfp = kj::newPromiseAndCrossThreadFulfiller<Result<ResultT>>();
        detail::deserPool.enqueue([&, &executor{kj::getCurrentThreadExecutor()}] {
            try {
                detail::IndirectSource wrapped{executor, from};
                if constexpr (std::is_void_v<ResultT>) {
                    fn(wrapped);
                    pfp.fulfiller->fulfill(result::success());
                } else {
                    pfp.fulfiller->fulfill(fn(wrapped));
                }
            } catch (...) {
                pfp.fulfiller->fulfill(result::current_exception());
            }
        });
        co_return LIX_TRY_AWAIT(pfp.promise);
    } catch (...) {
        co_return result::current_exception();
    }
#endif
}
}
