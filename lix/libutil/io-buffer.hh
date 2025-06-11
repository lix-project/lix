#pragma once
///@file IO buffer abstraction for use by buffered IO types.

#include <cstddef>
#include <memory>
#include <span>

namespace nix {

/**
 * A single-threaded read/write IO buffer of fixed size. Adding data to the write side
 * of the buffer makes it available to the read buffer, consuming it from the read side
 * makes it available for future writes. Once the buffer is full no further data may be
 * added, once it is empty no further data can be removed.
 */
class IoBuffer
{
    size_t bufSize, bufBegin{0}, bufUsed{0};
    std::unique_ptr<char[]> buffer;

public:
    explicit IoBuffer(size_t bufSize = 32 * 1024) : bufSize(bufSize) {}

    size_t size() const
    {
        return bufSize;
    }

    size_t used() const
    {
        return bufUsed;
    }

    /**
     * Return a subspan of the buffer that contains valid data. The returned
     * span might not cover the entire buffer if `used() > 0`. All reads must be
     * followed by a call to `consumed()` to remove bytes from the read buffer
     * and make them available for use by the write buffer.
     */
    std::span<const char> getReadBuffer();

    /**
     * Mark the first `size` bytes of the read buffer as consumed. `size` may
     * not exceed `used()`, but may exceed `getReadBuffer().size()`.
     */
    void consumed(size_t size);

    /**
     * Return a subspan of the buffer that may be written into. The returned
     * span may not cover the entire buffer if `used() > 0`. All writes must be
     * followed by calls to `added(n)` to mark the written bytes as readable.
     */
    std::span<char> getWriteBuffer();

    /**
     * Mark the first `size` bytes of the write buffer as readable. `size` may
     * not exceed `size() - used()`, but may exceed `getWriteBuffer().size()`.
     */
    void added(size_t size);
};

}
