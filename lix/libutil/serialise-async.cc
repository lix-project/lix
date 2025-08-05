#include "serialise-async.hh"

namespace nix {
size_t detail::UnbufferedAsyncSource::read(char * data, size_t len)
{
    if (auto got = from.read(data, len).wait(ws).value(); got) {
        return *got;
    } else {
        throw EndOfFile("async stream ended");
    }
}

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
}
