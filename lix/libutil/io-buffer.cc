#include "io-buffer.hh"
#include <cassert>

namespace nix {

std::span<const char> IoBuffer::getReadBuffer()
{
    const auto used = std::min(bufSize - bufBegin, bufUsed);
    return {buffer.get() + bufBegin, used};
}

void IoBuffer::consumed(size_t size)
{
    assert(size <= bufUsed);
    bufBegin = (bufBegin + size) % bufSize;
    bufUsed -= size;
    if (bufUsed == 0) {
        bufBegin = 0;
    }
}

std::span<char> IoBuffer::getWriteBuffer()
{
    if (!buffer) {
        buffer.reset(new char[bufSize]);
    }
    const auto bufEnd = (bufBegin + bufUsed) % bufSize;
    const auto free = std::min(bufSize - bufEnd, bufSize - bufUsed);
    return {buffer.get() + bufEnd, free};
}

void IoBuffer::added(size_t size)
{
    assert(size <= bufSize - bufUsed);
    bufUsed += size;
}

}
