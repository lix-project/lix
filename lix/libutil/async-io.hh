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
#include <kj/async-io.h>
#include <kj/async-unix.h>
#include <kj/async.h>
#include <kj/common.h>
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

    AsyncBufferedInputStream(AsyncInputStream & inner, size_t bufSize = 32 * 1024)
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

    AsyncBufferedOutputStream(AsyncOutputStream & inner, size_t bufSize = 32 * 1024)
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
}
