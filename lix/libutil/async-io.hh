#pragma once
///@file

#include "lix/libutil/async.hh"
#include "lix/libutil/box_ptr.hh"
#include "lix/libutil/charptr-cast.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/io-buffer.hh"
#include "lix/libutil/ref.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/serialise.hh"
#include <concepts>
#include <kj/async-io.h>
#include <kj/async-unix.h>
#include <kj/async.h>
#include <kj/common.h>
#include <kj/function.h>
#include <memory>
#include <string_view>

namespace nix {

class AsyncOutputStream;

// not derived from kj's AsyncInputStream because read and tryRead are already
// taken as method names, we don't need the other functions, and the bit about
// minBytes does not work well with our current io model. some day, who knows?
class AsyncInputStream : private kj::AsyncObject
{
public:
    virtual ~AsyncInputStream() noexcept(false) {}

    // expected to return none only on EOF or when `size = 0` was explicitly set.
    virtual kj::Promise<Result<std::optional<size_t>>> read(void * buffer, size_t size) = 0;

    /**
     * Read between `min` and `max` bytes or return `nullopt` if the stream ends
     * early. The buffer may be partially filled even when `nullopt` is returned
     * without giving an indication of how many bytes were read from the stream;
     * reading less than `min` bytes must be considered a fatal error condition.
     */
    kj::Promise<Result<std::optional<size_t>>> readRange(void * buffer, size_t min, size_t max)
    try {
        size_t total = 0;
        auto bufferC = charptr_cast<char *>(buffer);
        while (total < min) {
            if (auto got = LIX_TRY_AWAIT(read(bufferC + total, max - total)); !got) {
                co_return std::nullopt;
            } else {
                total += *got;
            }
        }
        co_return total;
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<void>> drainInto(Sink & sink);
    kj::Promise<Result<void>> drainInto(AsyncOutputStream & stream);

    kj::Promise<Result<std::string>> drain();
};

class AsyncSourceInputStream : public AsyncInputStream
{
    Source & inner;
    // inner must reference owned if owned is set. we'll keep a unique_ptr
    // field around in all instances to avoid duplicating the entire class
    // into a reference variant and an owning variant (holding a box_ptr).
    std::unique_ptr<Source> owned;

public:
    AsyncSourceInputStream(Source & inner) : inner(inner) {}
    AsyncSourceInputStream(box_ptr<Source> inner) : inner(*inner), owned(std::move(inner).take()) {}

    kj::Promise<Result<std::optional<size_t>>> read(void * buffer, size_t size) override;
};

class AsyncStringInputStream : public AsyncInputStream
{
    std::string_view s;

public:
    explicit AsyncStringInputStream(std::string_view s) : s(s) {}

    kj::Promise<Result<std::optional<size_t>>> read(void * buffer, size_t size) override;
};

// this writes to sources instead of async streams because none of the sinks
// we need to date are actually async, not because that wouldn't be possible
class AsyncTeeInputStream : public AsyncInputStream
{
    AsyncInputStream & inner;
    Sink & sink;

public:
    AsyncTeeInputStream(AsyncInputStream & inner, Sink & sink) : inner(inner), sink(sink) {}

    kj::Promise<Result<std::optional<size_t>>> read(void * buffer, size_t size) override;
};

class AsyncGeneratorInputStream : public AsyncInputStream
{
private:
    Generator<Bytes> g;
    Bytes buf;

public:
    AsyncGeneratorInputStream(Generator<Bytes> && g) : g(std::move(g)) {}

    kj::Promise<Result<std::optional<size_t>>> read(void * data, size_t len) override;
};

class AsyncBufferedInputStream : public AsyncInputStream
{
    AsyncInputStream & inner;
    ref<IoBuffer> buffer;

public:
    AsyncBufferedInputStream(AsyncInputStream & inner, ref<IoBuffer> buffer)
        : inner(inner)
        , buffer(buffer)
    {
    }

    AsyncBufferedInputStream(AsyncInputStream & inner, size_t bufSize = 32ul * 1024)
        : AsyncBufferedInputStream(inner, make_ref<IoBuffer>(bufSize))
    {
    }

    KJ_DISALLOW_COPY_AND_MOVE(AsyncBufferedInputStream);

    kj::Promise<Result<std::optional<size_t>>> read(void * data, size_t size) override;

    IoBuffer & getBuffer()
    {
        return *buffer;
    }
};

class AsyncOutputStream : private kj::AsyncObject
{
public:
    virtual ~AsyncOutputStream() noexcept(false) {}

    virtual kj::Promise<Result<size_t>> write(const void * src, size_t size) = 0;

    kj::Promise<Result<void>> writeFull(const void * src, size_t size)
    {
        return write(src, size).then(
            [this, src, size](Result<size_t> wrote) -> kj::Promise<Result<void>> {
                if (!wrote.has_value()) {
                    return {wrote.error()};
                } else if (wrote.value() == size) {
                    return {result::success()};
                } else {
                    return writeFull(
                        static_cast<const char *>(src) + wrote.value(), size - wrote.value()
                    );
                }
            }
        );
    }
};

class AsyncBufferedOutputStream : public AsyncOutputStream
{
    AsyncOutputStream & inner;
    ref<IoBuffer> buffer;

public:
    AsyncBufferedOutputStream(AsyncOutputStream & inner, ref<IoBuffer> buffer)
        : inner(inner)
        , buffer(buffer)
    {
    }

    AsyncBufferedOutputStream(AsyncOutputStream & inner, size_t bufSize = 32ul * 1024)
        : AsyncBufferedOutputStream(inner, make_ref<IoBuffer>(bufSize))
    {
    }

    KJ_DISALLOW_COPY_AND_MOVE(AsyncBufferedOutputStream);

    kj::Promise<Result<size_t>> write(const void * src, size_t size) override;
    kj::Promise<Result<void>> flush();

    IoBuffer & getBuffer()
    {
        return *buffer;
    }
};

class AsyncStream : public AsyncInputStream, public AsyncOutputStream
{};

class AsyncFdIoStream : public AsyncStream
{
    int fd;
    FdBlockingState oldState;
    AutoCloseFD ownedFd; // only for closing automatically, must equal fd if set
    kj::UnixEventPort::FdObserver observer;

public:
    struct shared_fd
    {};

    explicit AsyncFdIoStream(AutoCloseFD fd);
    AsyncFdIoStream(shared_fd, int fd);

    ~AsyncFdIoStream() noexcept(false);

    int getFD() const
    {
        return fd;
    }

    kj::Promise<Result<std::optional<size_t>>> read(void * tgt, size_t size) override;
    kj::Promise<Result<size_t>> write(const void * src, size_t size) override;
};

/**
 * A stream that reads a distinct format of concatenated chunks back into its
 * logical form, in order to guarantee a known state to the original stream,
 * even in the event of errors.
 *
 * Use with AsyncFramedOutputStream, which also allows the logical stream to be terminated
 * in the event of an exception.
 */
class AsyncFramedInputStream : public AsyncInputStream
{
private:
    AsyncInputStream & from;
    bool eof = false;
    /** Full contents of the current data frame. */
    std::vector<char> pending;
    /** Read offset into `pending`. The frame is fully processed if `pos == pending.size()`. */
    size_t pos = 0;

public:
    AsyncFramedInputStream(AsyncInputStream & from) : from(from) {}

    ~AsyncFramedInputStream();

    kj::Promise<Result<void>> finish();

    kj::Promise<Result<std::optional<size_t>>> read(void * buffer, size_t size) override;
};

/**
 * Write as chunks in the format expected by FramedSource.
 */
class AsyncFramedOutputStream : public AsyncOutputStream
{
    AsyncOutputStream & to;

public:
    explicit AsyncFramedOutputStream(AsyncOutputStream & to) : to(to) {}

    kj::Promise<Result<void>> finish();

    kj::Promise<Result<size_t>> write(const void * src, size_t size) override;
};

struct AsyncPipe
{
    std::unique_ptr<AsyncInputStream> reader;
    std::unique_ptr<AsyncOutputStream> writer;
};

/**
 * Creates a zero-copy pipe that runs entirely inside the event loop. Calls to
 * `writer.write` will not return before before the reader has either read all
 * data passed to `write` or has been dropped. Multiple writers are forbidden;
 * multiple readers are allowed, but are serviced in some indeterminate order.
 */
AsyncPipe newZeroCopyPipe();

/**
 * Adapts an `AsyncInputStream`-consuming promise created by a callback into a
 * pair of `AsyncOutputStream` and a callback that awaits the wrapped promise.
 * Errors flow to both the writer *and* the promise created by the callback we
 * return here. Awaiting the callback also invalidates the transfer stream; if
 * the writer isn't done by then it'll receive an exception on the next write.
 */
template<std::invocable<AsyncInputStream &> Fn>
    requires requires(Fn fn, AsyncInputStream & i, AsyncIoRoot aio) {
        []<typename T>(kj::Promise<Result<T>>) {}(fn(i));
    }
auto wrapInAsyncPipe(Fn && fn)
{
    using promise_type = std::invoke_result_t<Fn, AsyncInputStream &>;
    using result_type = decltype(std::declval<promise_type>().wait(std::declval<kj::WaitScope &>()));

    struct State
    {
        promise_type inner;
        std::exception_ptr error;
        bool finished = false;

        State(Fn && fn, std::unique_ptr<AsyncInputStream> reader)
            : inner(fn(*reader)
                        .attach(std::move(reader))
                        .then([this](auto result) {
                            if (result.has_error()) {
                                error = result.error();
                            }
                            return result;
                        })
                        .eagerlyEvaluate([this](kj::Exception && e) -> result_type {
                            try {
                                kj::throwFatalException(std::move(e));
                            } catch (...) {
                                error = std::current_exception();
                                throw;
                            }
                        }))
        {
        }
    };

    struct Writer : AsyncOutputStream
    {
        ref<State> state;
        std::unique_ptr<AsyncOutputStream> out;

        Writer(ref<State> state, std::unique_ptr<AsyncOutputStream> out) : state(state), out(std::move(out)) {}

        kj::Promise<Result<size_t>> write(const void * src, size_t size) override
        try {
            if (state->error) {
                co_return result::failure(state->error);
            } else if (state->finished) {
                throw Error("stream already closed");
            }
            co_return LIX_TRY_AWAIT(out->write(src, size));
        } catch (...) {
            // do not report this exception if `error` was set; we want to give
            // priority to `runInner` for error reporting since the pipe itself
            // is of little interest. if `inner` fails the pipe will break, all
            // subsequent writes to the pipe will throw. only the `inner` error
            // is actually interesting in this case, broken pipe errors aren't.
            if (state->error) {
                co_return result::failure(state->error);
            }
            state->error = std::current_exception();
            co_return result::current_exception();
        }
    };

    auto [reader, writer] = newZeroCopyPipe();
    auto state = make_ref<State>(std::forward<Fn>(fn), std::move(reader));
    return std::pair{
        std::make_unique<Writer>(state, std::move(writer)),
        kj::Function<promise_type()>{[state]() {
            state->finished = true;
            return std::move(state->inner);
        }},
    };
}
}
