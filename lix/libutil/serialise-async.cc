#include "serialise-async.hh"

namespace nix {
#if !defined(HAVE_THREADBARE_LIBC)
size_t detail::BufferedAsyncSource::read(char * data, size_t len)
{
    auto & buf = from.getBuffer();
    if (auto avail = buf.getReadBuffer(); !avail.empty()) {
        len = std::min(len, avail.size());
        memcpy(data, avail.data(), len);
        buf.consumed(len);
        return len;
    } else if (auto got = from.read(data, len).wait(ws).value(); got) {
        return *got;
    } else {
        throw EndOfFile("async stream ended");
    }
}
#else
size_t detail::IndirectSource::read(char * data, size_t len)
{
    if (auto avail = from.getBuffer().getReadBuffer(); !avail.empty()) {
        len = std::min(len, avail.size());
        memcpy(data, avail.data(), len);
        from.getBuffer().consumed(len);
        return len;
    }

    auto got = executor.executeSync([&] { return from.read(data, len); }).value();
    if (!got) {
        throw EndOfFile("indirect source ended");
    }
    return *got;
}

ThreadPool detail::deserPool{"deser pool"};
#endif
}
