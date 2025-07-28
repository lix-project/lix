#include "lix/libutil/async-io.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/result.hh"
#include <cstring>
#include <exception>
#include <gtest/gtest.h>
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
}
