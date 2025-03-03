#include "async-io.hh"

namespace nix {
kj::Promise<Result<void>> AsyncInputStream::drainInto(Sink & sink)
try {
    constexpr size_t BUF_SIZE = 65536;
    auto buf = std::make_unique<char[]>(BUF_SIZE);
    while (auto r = TRY_AWAIT(read(buf.get(), BUF_SIZE))) {
        sink(std::string_view(buf.get(), r));
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

kj::Promise<Result<size_t>> AsyncSourceInputStream::read(void * buffer, size_t size)
try {
    while (true) {
        if (auto got = inner.read(static_cast<char *>(buffer), size); got > 0) {
            return {result::success(got)};
        }
    }
} catch (EndOfFile &) {
    return {result::success(0)};
} catch (...) {
    return {result::current_exception()};
}

kj::Promise<Result<size_t>> AsyncStringInputStream::read(void * buffer, size_t size)
{
    size = std::min(size, s.size());
    if (size > 0) {
        memcpy(buffer, s.data(), size);
        s.remove_prefix(size);
    }
    return {result::success(size)};
}

kj::Promise<Result<size_t>> AsyncTeeInputStream::read(void * buffer, size_t size)
try {
    auto got = TRY_AWAIT(inner.read(buffer, size));
    sink({static_cast<char *>(buffer), got});
    co_return got;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<size_t>> AsyncGeneratorInputStream::read(void * data, size_t len)
try {
    while (!buf.size()) {
        if (auto next = g.next()) {
            buf = *next;
        } else {
            return {result::success(0)};
        }
    }

    len = std::min(len, buf.size());
    memcpy(data, buf.data(), len);
    buf = buf.subspan(len);
    return {{len}};
} catch (...) {
    return {result::current_exception()};
}

kj::Promise<Result<size_t>> AsyncFdInputStream::read(void * buffer, size_t size)
{
    if (auto got = ::read(fd, buffer, size); got >= 0) {
        return {result::success(size_t(got))};
    } else {
        return {result::failure(std::make_exception_ptr(SysError(errno, "read failed")))};
    }
}

IndirectAsyncInputStreamToSource::IndirectAsyncInputStreamToSource(AsyncInputStream & source)
    : source(source)
    , pipe([&] {
        auto pfp = kj::newPromiseAndCrossThreadFulfiller<Request>();
        return Pipe{std::move(pfp.fulfiller), std::move(pfp.promise)};
    }())
{
}

IndirectAsyncInputStreamToSource::~IndirectAsyncInputStreamToSource() noexcept(true)
{
    if (pipe.sendRequest->isWaiting()) {
        pipe.sendRequest->fulfill(Request{nullptr, 0, {}});
    }
}

kj::Promise<void> IndirectAsyncInputStreamToSource::feed()
{
    while (true) {
        auto req = co_await pipe.nextRequest;
        if (req.data == nullptr) {
            break;
        }
        try {
            auto got = (co_await source.read(req.data, req.len)).value();
            if (req.len != 0 && got == 0) {
                auto eof = std::make_exception_ptr(EndOfFile("async input finished"));
                req.result.set_exception(eof);
                break;
            } else {
                auto pfp = kj::newPromiseAndCrossThreadFulfiller<Request>();
                req.result.set_value(std::pair{got, std::move(pfp.fulfiller)});
                pipe.nextRequest = std::move(pfp.promise);
            }
        } catch (...) {
            req.result.set_exception(std::current_exception());
            co_return;
        }
    }
}

size_t IndirectAsyncInputStreamToSource::read(char * data, size_t len)
{
    std::promise<std::pair<size_t, kj::Own<kj::CrossThreadPromiseFulfiller<Request>>>> promise;
    auto future = promise.get_future();
    pipe.sendRequest->fulfill(Request{data, len, std::move(promise)});
    auto [result, next] = future.get();
    pipe.sendRequest = std::move(next);
    return result;
}
}
