#include "lix/libutil/async-io.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/result.hh"
#include <cstring>
#include <exception>
#include <gtest/gtest.h>
#include <kj/async.h>
#include <optional>
#include <string>

namespace nix {

namespace {
struct TestError : Error
{
    using Error::Error;
};
}

TEST(AsyncInputStream, readFullError)
{
    struct BadStream : AsyncInputStream
    {
        kj::Promise<Result<std::optional<size_t>>> read(void * buffer, size_t size) override
        {
            memcpy(buffer, "foo", std::min<size_t>(size, 3));
            return {result::failure(std::make_exception_ptr(TestError("bad")))};
        }
    };

    AsyncIoRoot aio;
    char buf[8];
    ASSERT_THROW(aio.blockOn(BadStream{}.readRange(buf, sizeof(buf), sizeof(buf))), TestError);
}

TEST(AsyncInputStream, readFullLoop)
{
    struct ChunkStream : AsyncInputStream
    {
        int left = 10;
        kj::Promise<Result<std::optional<size_t>>> read(void * buffer, size_t size) override
        {
            if (left == 0 || size == 0) {
                return {{std::nullopt}};
            } else {
                memcpy(buffer, std::to_string(10 - left).c_str(), 1);
                left -= 1;
                return {{1}};
            }
        }
    };

    AsyncIoRoot aio;
    ChunkStream in;

    {
        char buf[8] = {};
        // a bit
        ASSERT_EQ(aio.blockOn(in.readRange(buf, 3, 3)), 3);
        ASSERT_STREQ(buf, "012");
        // nothing got eaten
        ASSERT_EQ(in.left, 7);
        // the rest
        ASSERT_EQ(aio.blockOn(in.readRange(buf, 7, 7)), 7);
        ASSERT_STREQ(buf, "3456789");
    }

    // eof aborts read
    in.left = 5;
    {
        char buf[8] = {};
        ASSERT_EQ(aio.blockOn(in.readRange(buf, 8, 8)), std::nullopt);
    }
}

TEST(AsyncInputStream, readFullOverMin)
{
    struct FillStream : AsyncInputStream
    {
        kj::Promise<Result<std::optional<size_t>>> read(void * buffer, size_t size) override
        {
            memset(buffer, 0, size);
            return {{size}};
        }
    };

    AsyncIoRoot aio;
    FillStream in;

    char buf[8];
    ASSERT_EQ(aio.blockOn(in.readRange(buf, 3, 8)), 8);
}

TEST(AsyncZeroCopyPipe, exactRead)
{
    AsyncIoRoot aio;
    auto [r, w] = newZeroCopyPipe();

    char buf[64] = {};
    auto rp = r->read(buf, 4);
    ASSERT_FALSE(rp.poll(aio.kj.waitScope));
    auto wp = w->write("test", 4);
    ASSERT_TRUE(rp.poll(aio.kj.waitScope));
    ASSERT_EQ(rp.wait(aio.kj.waitScope).value(), 4);
    ASSERT_STREQ(buf, "test");
    ASSERT_TRUE(wp.poll(aio.kj.waitScope));
    ASSERT_EQ(wp.wait(aio.kj.waitScope).value(), 4);
}

TEST(AsyncZeroCopyPipe, oversizeReadTruncates)
{
    AsyncIoRoot aio;
    auto [r, w] = newZeroCopyPipe();

    char buf[64] = {};
    auto rp = r->read(buf, 64);
    ASSERT_FALSE(rp.poll(aio.kj.waitScope));
    auto wp = w->write("test", 4);
    ASSERT_TRUE(rp.poll(aio.kj.waitScope));
    ASSERT_EQ(rp.wait(aio.kj.waitScope).value(), 4);
    ASSERT_STREQ(buf, "test");
    ASSERT_TRUE(wp.poll(aio.kj.waitScope));
    ASSERT_EQ(wp.wait(aio.kj.waitScope).value(), 4);
}

TEST(AsyncZeroCopyPipe, undersizeReadBuffers)
{
    AsyncIoRoot aio;
    auto [r, w] = newZeroCopyPipe();

    char buf[64] = {};
    auto rp = r->read(buf, 2);
    ASSERT_FALSE(rp.poll(aio.kj.waitScope));
    auto wp = w->write("test", 4);
    ASSERT_TRUE(rp.poll(aio.kj.waitScope));
    ASSERT_EQ(rp.wait(aio.kj.waitScope).value(), 2);
    ASSERT_STREQ(buf, "te");
    ASSERT_FALSE(wp.poll(aio.kj.waitScope));
    rp = r->read(buf, 3);
    ASSERT_TRUE(rp.poll(aio.kj.waitScope));
    ASSERT_EQ(rp.wait(aio.kj.waitScope).value(), 2);
    ASSERT_STREQ(buf, "st");
    ASSERT_TRUE(wp.poll(aio.kj.waitScope));
    ASSERT_EQ(wp.wait(aio.kj.waitScope).value(), 4);
}

TEST(AsyncZeroCopyPipe, emptyWrite)
{
    AsyncIoRoot aio;
    auto [r, w] = newZeroCopyPipe();

    char buf[64] = {};
    auto rp = r->read(buf, 4);
    ASSERT_FALSE(rp.poll(aio.kj.waitScope));
    w->write("", 0).wait(aio.kj.waitScope).value();
    ASSERT_FALSE(rp.poll(aio.kj.waitScope));
    w->write("test", 4).wait(aio.kj.waitScope).value();
    ASSERT_TRUE(rp.poll(aio.kj.waitScope));
    ASSERT_EQ(rp.wait(aio.kj.waitScope).value(), 4);
    ASSERT_STREQ(buf, "test");
}

TEST(AsyncZeroCopyPipe, emptyRead)
{
    AsyncIoRoot aio;
    auto [r, w] = newZeroCopyPipe();

    char buf[64] = {};
    auto rp = r->read(buf, 0);
    ASSERT_FALSE(rp.poll(aio.kj.waitScope));
    auto wp = w->write("test", 4);
    ASSERT_TRUE(rp.poll(aio.kj.waitScope));
    ASSERT_EQ(rp.wait(aio.kj.waitScope).value(), 0);
    ASSERT_FALSE(wp.poll(aio.kj.waitScope));
    rp = r->read(buf, 4);
    ASSERT_TRUE(rp.poll(aio.kj.waitScope));
    ASSERT_EQ(rp.wait(aio.kj.waitScope).value(), 4);
    ASSERT_STREQ(buf, "test");
}

TEST(AsyncZeroCopyPipe, readCancelWorks)
{
    AsyncIoRoot aio;
    auto [r, w] = newZeroCopyPipe();

    char buf[64] = {};
    auto rp = r->read(buf, 64);
    ASSERT_FALSE(rp.poll(aio.kj.waitScope));
    // restart the read, dropping the old promise
    rp = r->read(buf, 64);
    ASSERT_FALSE(rp.poll(aio.kj.waitScope));
    auto wp = w->write("test", 4);
    ASSERT_TRUE(rp.poll(aio.kj.waitScope));
    ASSERT_EQ(rp.wait(aio.kj.waitScope).value(), 4);
    ASSERT_STREQ(buf, "test");
    ASSERT_TRUE(wp.poll(aio.kj.waitScope));
    ASSERT_EQ(wp.wait(aio.kj.waitScope).value(), 4);
}

TEST(AsyncZeroCopyPipe, writeCancelBreaks)
{
    AsyncIoRoot aio;
    auto [r, w] = newZeroCopyPipe();

    (void) w->write("test", 4);

    char buf[64] = {};
    ASSERT_THROW(r->read(buf, sizeof(buf)).wait(aio.kj.waitScope).value(), Error);
    ASSERT_THROW(w->write(buf, sizeof(buf)).wait(aio.kj.waitScope).value(), Error);
}

TEST(AsyncZeroCopyPipe, multipleWritersBreakPipe)
{
    AsyncIoRoot aio;
    auto [r, w] = newZeroCopyPipe();

    auto wp = w->write("test", 4);
    ASSERT_THROW(w->write("test", 4).wait(aio.kj.waitScope).value(), Error);
    ASSERT_THROW(wp.wait(aio.kj.waitScope).value(), Error);
    char buf[64] = {};
    ASSERT_THROW(r->read(buf, sizeof(buf)).wait(aio.kj.waitScope).value(), Error);
}

TEST(AsyncZeroCopyPipe, multipleReaders)
{
    AsyncIoRoot aio;
    auto [r, w] = newZeroCopyPipe();

    char buf1[64] = {};
    char buf2[64] = {};

    {
        auto rp1 = r->read(buf1, 64);
        auto rp2 = r->read(buf2, 64);

        w->write("test1", 5).wait(aio.kj.waitScope).value();
        w->write("test2", 5).wait(aio.kj.waitScope).value();

        ASSERT_EQ(rp1.wait(aio.kj.waitScope).value(), 5);
        ASSERT_EQ(rp2.wait(aio.kj.waitScope).value(), 5);
        ASSERT_EQ(buf1[4] + buf2[4], '1' + '2');
    }

    {
        auto rp1 = r->read(buf1, 1);
        auto rp2 = r->read(buf2, 1);

        w->write("12", 2).wait(aio.kj.waitScope).value();

        ASSERT_EQ(rp1.wait(aio.kj.waitScope).value(), 1);
        ASSERT_EQ(rp2.wait(aio.kj.waitScope).value(), 1);
        ASSERT_EQ(buf1[0] + buf2[0], '1' + '2');
    }
}

TEST(AsyncZeroCopyPipe, dropReader)
{
    AsyncIoRoot aio;
    auto [r, w] = newZeroCopyPipe();

    auto wp = w->write("test", 4);
    (void) auto(std::move(r));
    ASSERT_THROW(wp.wait(aio.kj.waitScope).value(), Error);
}

TEST(AsyncZeroCopyPipe, dropWriter)
{
    AsyncIoRoot aio;
    auto [r, w] = newZeroCopyPipe();

    char buf[64] = {};
    auto rp = r->read(buf, 64);
    (void) auto(std::move(w));
    ASSERT_EQ(rp.wait(aio.kj.waitScope).value(), std::nullopt);
}
}
