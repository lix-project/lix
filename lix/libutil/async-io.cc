#include "async-io.hh"
#include "async.hh"
#include "error.hh"
#include "file-descriptor.hh"
#include "logging.hh"
#include "result.hh"
#include "serialise.hh"
#include <cerrno>
#include <exception>
#include <fcntl.h>

namespace nix {
kj::Promise<Result<void>> AsyncInputStream::drainInto(Sink & sink)
try {
    constexpr size_t BUF_SIZE = 65536;
    auto buf = std::make_unique<char[]>(BUF_SIZE);
    while (auto r = TRY_AWAIT(read(buf.get(), BUF_SIZE))) {
        sink(std::string_view(buf.get(), *r));
    }
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> AsyncInputStream::drainInto(AsyncOutputStream & stream)
try {
    constexpr size_t BUF_SIZE = 65536;
    auto buf = std::make_unique<char[]>(BUF_SIZE);
    while (auto r = TRY_AWAIT(read(buf.get(), BUF_SIZE))) {
        TRY_AWAIT(stream.writeFull(buf.get(), *r));
    }
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::string>> AsyncInputStream::drain()
try {
    StringSink s;
    TRY_AWAIT(drainInto(s));
    co_return std::move(s.s);
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::optional<size_t>>> AsyncSourceInputStream::read(void * buffer, size_t size)
try {
    while (true) {
        if (auto got = inner.read(static_cast<char *>(buffer), size); got > 0) {
            return {result::success(got)};
        }
    }
} catch (EndOfFile &) {
    return {result::success(std::nullopt)};
} catch (...) {
    return {result::current_exception()};
}

kj::Promise<Result<std::optional<size_t>>> AsyncStringInputStream::read(void * buffer, size_t size)
{
    size = std::min(size, s.size());
    if (size == 0) {
        return {result::success(std::nullopt)};
    }

    memcpy(buffer, s.data(), size);
    s.remove_prefix(size);
    return {result::success(size)};
}

kj::Promise<Result<std::optional<size_t>>> AsyncTeeInputStream::read(void * buffer, size_t size)
try {
    auto got = TRY_AWAIT(inner.read(buffer, size));
    if (got) {
        sink({static_cast<char *>(buffer), *got});
    }
    co_return got;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::optional<size_t>>> AsyncGeneratorInputStream::read(void * data, size_t len)
try {
    if (len == 0) {
        return {result::success(std::nullopt)};
    }

    while (!buf.size()) {
        if (auto next = g.next()) {
            buf = *next;
        } else {
            return {result::success(std::nullopt)};
        }
    }

    len = std::min(len, buf.size());
    memcpy(data, buf.data(), len);
    buf = buf.subspan(len);
    return {{len}};
} catch (...) {
    return {result::current_exception()};
}

kj::Promise<Result<std::optional<size_t>>> AsyncBufferedInputStream::read(void * data, size_t size)
try {
    while (buffer->used() == 0) {
        const auto space = buffer->getWriteBuffer();
        const auto got = TRY_AWAIT(inner.read(space.data(), space.size()));
        if (!got) {
            co_return std::nullopt;
        }
        buffer->added(*got);
    }

    const auto available = buffer->getReadBuffer();
    const auto n = std::min(size, available.size());
    memcpy(data, available.data(), n);
    buffer->consumed(n);
    co_return result::success(n);
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<size_t>> AsyncBufferedOutputStream::write(const void * src, size_t size)
try {
    if (size > buffer->size()) {
        TRY_AWAIT(flush());
        co_return TRY_AWAIT(inner.write(src, size));
    }

    if (size > buffer->getWriteBuffer().size()) {
        TRY_AWAIT(flush());
    }

    const auto into = buffer->getWriteBuffer();
    memcpy(into.data(), src, size);
    buffer->added(size);
    co_return size;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> AsyncBufferedOutputStream::flush()
try {
    if (auto unsent = buffer->getReadBuffer(); !unsent.empty()) {
        TRY_AWAIT(inner.writeFull(unsent.data(), unsent.size()));
        buffer->consumed(unsent.size());
    }
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

AsyncFdIoStream::AsyncFdIoStream(AutoCloseFD fd) : AsyncFdIoStream(shared_fd{}, fd.get())
{
    ownedFd = std::move(fd);
}

AsyncFdIoStream::AsyncFdIoStream(shared_fd, int fd)
    : fd(fd)
    , oldState(makeNonBlocking(fd))
    , observer(AIO().unixEventPort, fd, kj::UnixEventPort::FdObserver::OBSERVE_READ_WRITE)
{
}

AsyncFdIoStream::~AsyncFdIoStream() noexcept(false)
{
    try {
        resetBlockingState(fd, oldState);
    } catch (...) {
        ignoreExceptionInDestructor();
    }
}

kj::Promise<Result<std::optional<size_t>>> AsyncFdIoStream::read(void * tgt, size_t size)
{
    auto got = ::read(fd, tgt, size);
    if (got > 0) {
        return {result::success(got)};
    } else if (got == 0) {
        return {result::success(std::nullopt)};
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return observer.whenBecomesReadable().then([=, this] { return read(tgt, size); });
    } else {
        return {result::failure(std::make_exception_ptr(SysError(errno, "read failed")))};
    }
}

kj::Promise<Result<size_t>> AsyncFdIoStream::write(const void * src, size_t size)
{
    auto got = ::write(fd, src, size);
    if (got >= 0) {
        return {result::success(got)};
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return observer.whenBecomesWritable().then([=, this] { return write(src, size); });
    } else {
        return {result::failure(std::make_exception_ptr(SysError(errno, "write failed")))};
    }
}
AsyncFramedInputStream::~AsyncFramedInputStream()
{
    if (!eof) {
        printError(
            "AsyncFramedInputStream wasn't read to finish! its connection is now probably broken."
        );
    }
}

kj::Promise<Result<void>> AsyncFramedInputStream::finish()
try {
    if (!eof) {
        while (true) {
            auto n = TRY_AWAIT(readNum<unsigned>(from));
            if (!n) {
                eof = true;
                break;
            }
            std::vector<char> data(n);
            if (TRY_AWAIT(from.readRange(data.data(), n, n)) != n) {
                throw Error("framed stream ended unexpectedly");
            }
        }
    }
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::optional<size_t>>> AsyncFramedInputStream::read(void * buffer, size_t size)
try {
    if (eof) {
        co_return std::nullopt;
    }

    if (pos >= pending.size()) {
        size_t len = TRY_AWAIT(readNum<unsigned>(from));
        if (!len) {
            eof = true;
            co_return std::nullopt;
        }
        pending = std::vector<char>(len);
        pos = 0;
        if (TRY_AWAIT(from.readRange(pending.data(), len, len)) != len) {
            throw Error("framed stream ended unexpectedly");
        }
    }

    auto n = std::min(size, pending.size() - pos);
    memcpy(buffer, pending.data() + pos, n);
    pos += n;
    co_return n;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> AsyncFramedOutputStream::finish()
try {
    StringSink tmp;
    tmp << 0;
    TRY_AWAIT(to.writeFull(tmp.s.data(), tmp.s.size()));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<size_t>> AsyncFramedOutputStream::write(const void * buffer, size_t size)
try {
    StringSink tmp;
    tmp << size;
    TRY_AWAIT(to.writeFull(tmp.s.data(), tmp.s.size()));
    TRY_AWAIT(to.writeFull(buffer, size));
    co_return size;
} catch (...) {
    co_return result::current_exception();
}
}
