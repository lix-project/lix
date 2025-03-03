#include "lix/libutil/async-io.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/result.hh"

#include <exception>
#include <gtest/gtest.h>
#include <stdexcept>

namespace nix {
TEST(IndirectAsyncInputStreamToSource, basic)
{
    struct Stream : AsyncInputStream
    {
        int round = 0;
        kj::Promise<Result<size_t>> read(void * buffer, size_t size) override
        {
            round++;
            if (round <= 10) {
                memset(buffer, size, 1);
                return {{1}};
            } else if (round <= 13) {
                memset(buffer, size, size);
                return {{size}};
            } else {
                return {result::success(0)};
            }
        }
    };

    AsyncIoRoot aio;
    Stream s;

    IndirectAsyncInputStreamToSource is(s);

    auto user = std::async(std::launch::async, [&]() {
        char buf[1026];

        // single read
        ASSERT_EQ(is.read(buf, 1), 1);
        ASSERT_EQ(buf[0], 1);

        // read spanning blocks doesn't coalesce
        ASSERT_EQ(is.read(buf, 5), 1);
        ASSERT_EQ(buf[0], 5);

        // coalescing from Source works
        ASSERT_NO_THROW(is(buf, 8));
        ASSERT_EQ(buf[0], 8);

        // next reads fill all sizes
        ASSERT_EQ(is.read(buf, 513), 513);
        ASSERT_EQ(buf[0], 1);
        ASSERT_EQ(is.read(buf, 1025), 1025);
        ASSERT_EQ(buf[0], 1);

        // zero-size reads don't EOF
        ASSERT_EQ(is.read(buf, 0), 0);

        // EOF propagates
        ASSERT_THROW(is.read(buf, 1025), EndOfFile);
    });

    is.feed().wait(aio.kj.waitScope);
    user.get();
}

TEST(IndirectAsyncInputStreamToSource, errorPropagation)
{
    struct Stream : AsyncInputStream
    {
        int round = 0;
        kj::Promise<Result<size_t>> read(void * buffer, size_t size) override
        {
            return {result::failure(std::make_exception_ptr(std::invalid_argument("foo")))};
        }
    };

    AsyncIoRoot aio;
    Stream s;

    IndirectAsyncInputStreamToSource is(s);

    auto user = std::async(std::launch::async, [&]() {
        char buf[1];
        ASSERT_THROW(is.read(buf, 1), std::invalid_argument);
    });

    is.feed().wait(aio.kj.waitScope);
    user.get();
}
}
