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
}
