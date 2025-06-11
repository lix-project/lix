#include "lix/libutil/io-buffer.hh"

#include <gtest/gtest.h>

namespace nix {

TEST(IoBuffer, works)
{
    IoBuffer buf{8};

    // empty buffer doesn't return anything
    ASSERT_EQ(buf.used(), 0);
    ASSERT_EQ(buf.getReadBuffer().size(), 0);

    // write a bit, it's no longer empty
    ASSERT_EQ(buf.getWriteBuffer().size(), 8);
    memcpy(buf.getWriteBuffer().data(), "test", 5);
    buf.added(5);

    // five bytes available now
    ASSERT_EQ(buf.used(), 5);
    ASSERT_EQ(buf.getReadBuffer().size(), 5);
    ASSERT_STREQ(buf.getReadBuffer().data(), "test");
    buf.consumed(5);
    ASSERT_EQ(buf.used(), 0);

    // write buffer resets to start of buffer when empty
    ASSERT_EQ(buf.getWriteBuffer().size(), 8);

    // not adding anything does nothing to the buffer
    ASSERT_EQ(buf.used(), 0);

    // buffer can wrap around the end, but in two segments
    ASSERT_EQ(buf.getWriteBuffer().size(), 8);
    memcpy(buf.getWriteBuffer().data(), "test", 5);
    buf.added(5);
    ASSERT_EQ(buf.getReadBuffer().size(), 5);
    buf.consumed(4);
    ASSERT_EQ(buf.getWriteBuffer().size(), 3);
    memcpy(buf.getWriteBuffer().data(), "12", 3);
    buf.added(3);
    ASSERT_EQ(buf.getWriteBuffer().size(), 4);
    memcpy(buf.getWriteBuffer().data(), "345", 4);
    buf.added(4);

    // reading now also happens in two chunks
    ASSERT_EQ(buf.used(), 8);
    ASSERT_EQ(buf.getReadBuffer().size(), 4);
    ASSERT_STREQ(buf.getReadBuffer().data(), "");
    buf.consumed(1);
    ASSERT_STREQ(buf.getReadBuffer().data(), "12");
    buf.consumed(3);
    ASSERT_EQ(buf.used(), 4);
    ASSERT_STREQ(buf.getReadBuffer().data(), "345");
    buf.consumed(4);

    // buffer is now empty again
    ASSERT_EQ(buf.used(), 0);
    ASSERT_EQ(buf.getWriteBuffer().size(), 8);
}

}
