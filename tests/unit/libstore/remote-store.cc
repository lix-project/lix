#include <gtest/gtest.h>

#include "lix/libstore/remote-store-connection.hh"

namespace nix {
namespace {

struct RemoteStoreLogTest : testing::Test, Logger, RemoteStore::Connection
{
    int fd = -1;
    Logger * previousLogger = logger;
    Verbosity previousVerbosity = getVerbosity();
    std::vector<std::pair<Verbosity, std::string>> messages;

    RemoteStoreLogTest()
    {
        logger = this;
        setVerbosity(lvlWarn);
    }

    ~RemoteStoreLogTest() override
    {
        logger = previousLogger;
        setVerbosity(previousVerbosity);
    }

    int getFD() const override
    {
        return fd;
    }

    BufferState log(Verbosity level, std::string_view text) override
    {
        messages.emplace_back(level, text);
        return BufferState::HasSpace;
    }

    BufferState logEI(const ErrorInfo & ei) override
    {
        return log(ei.level, ei.msg.str());
    }

    RemoteError process(const StringSink & frames)
    {
        AsyncIoRoot aio;
        auto sockets = SocketPair::stream();
        fd = sockets.a.get();
        writeFull(sockets.b.get(), frames.s);
        AsyncFdIoStream stream{AsyncFdIoStream::shared_fd{}, getFD()};
        return aio.blockOn(processStderr(stream));
    }
};

TEST_F(RemoteStoreLogTest, preservesWarningSeverity)
{
    StringSink frames;
    frames << STDERR_LOG << lvlWarn << "typed warning\n" << STDERR_LAST;

    ASSERT_FALSE(process(frames).e);
    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages[0].first, lvlWarn);
    EXPECT_EQ(messages[0].second, "typed warning");
}

TEST_F(RemoteStoreLogTest, consumesFilteredMessage)
{
    setVerbosity(lvlError);
    StringSink frames;
    frames << STDERR_LOG << lvlWarn << "filtered warning\n"
           << STDERR_LOG << lvlError << "following error\n"
           << STDERR_LAST;

    ASSERT_FALSE(process(frames).e);
    ASSERT_EQ(messages.size(), 1);
    EXPECT_EQ(messages[0].first, lvlError);
    EXPECT_EQ(messages[0].second, "following error");
}

TEST_F(RemoteStoreLogTest, rejectsInvalidSeverity)
{
    StringSink frames;
    frames << STDERR_LOG << unsigned(lvlVomit + 1) << "invalid message" << STDERR_LAST;

    try {
        (void) process(frames);
        FAIL() << "accepted an invalid log level";
    } catch (const Error & e) {
        EXPECT_NE(std::string(e.what()).find("got invalid log level"), std::string::npos);
    }
}

}
}
