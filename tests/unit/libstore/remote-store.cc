#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "lix/libstore/remote-store-connection.hh"
#include "lix/libstore/worker-protocol.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/serialise.hh"

namespace nix {
namespace {

struct CaptureLogger : Logger
{
    struct Start
    {
        ActivityId id;
        ActivityType type;
        std::string text;
    };

    std::vector<Start> starts;
    std::vector<ActivityId> stops;
    std::vector<ResultType> results;
    std::vector<std::string> buildLogLines;
    std::vector<std::string> messages;

    BufferState log(Verbosity, std::string_view s) override
    {
        messages.emplace_back(s);
        return BufferState::HasSpace;
    }

    BufferState logEI(const ErrorInfo & ei) override
    {
        messages.emplace_back(ei.msg.str());
        return BufferState::HasSpace;
    }

    BufferState startActivityImpl(
        ActivityId act, Verbosity, ActivityType type, const std::string & s, const Fields &, ActivityId
    ) override
    {
        starts.push_back({act, type, s});
        return BufferState::HasSpace;
    }

    BufferState stopActivityImpl(ActivityId act) override
    {
        stops.push_back(act);
        return BufferState::HasSpace;
    }

    BufferState resultImpl(ActivityId, ResultType type, const Fields & fields) override
    {
        results.push_back(type);
        if (type == resBuildLogLine && !fields.empty() && fields[0].type == Logger::Field::tString) {
            buildLogLines.push_back(fields[0].s);
        }
        return BufferState::HasSpace;
    }
};

struct TestConnection final : RemoteStore::Connection
{
    int fd;

    explicit TestConnection(int fd) : fd(fd)
    {
        store = nullptr;
    }

    int getFD() const override
    {
        return fd;
    }
};

void writeFields(StringSink & sink, const Logger::Fields & fields)
{
    sink << uint64_t(fields.size());
    for (auto & field : fields) {
        sink << uint64_t(field.type);
        if (field.type == Logger::Field::tInt) {
            sink << field.i;
        } else {
            sink << field.s;
        }
    }
}

void writeStartActivity(
    StringSink & sink, ActivityId act, ActivityType type, std::string_view text, const Logger::Fields & fields
)
{
    sink << uint64_t(STDERR_START_ACTIVITY) << act << uint64_t(lvlInfo) << uint64_t(type) << text;
    writeFields(sink, fields);
    sink << ActivityId(0);
}

void writeLast(StringSink & sink)
{
    sink << uint64_t(STDERR_LAST);
}

struct ScopedLogger
{
    Logger * old;

    explicit ScopedLogger(Logger & newLogger) : old(logger)
    {
        logger = &newLogger;
    }

    ~ScopedLogger()
    {
        logger = old;
    }
};

}

TEST(RemoteStoreConnection, activitiesOutliveProcessStderrCalls)
{
    CaptureLogger capture;
    ScopedLogger scopedLogger{capture};

    auto sockets = SocketPair::stream();
    TestConnection conn{sockets.a.get()};
    AsyncIoRoot aio;

    // 1st op: start + end stream.
    // It happens in practice at the moment because buffering the activity teardown can occur.
    {
        StringSink sink;
        writeStartActivity(
            sink,
            42,
            actBuild,
            "building 'probe'",
            {
                std::string("/nix/store/00000000000000000000000000000000-probe.drv"),
                std::string(""),
                uint64_t(1),
                uint64_t(1),
            }
        );
        writeLast(sink);
        writeFull(sockets.b.get(), sink.s);
    }
    {
        AsyncFdIoStream stream{AsyncFdIoStream::shared_fd{}, conn.getFD()};
        auto error = aio.blockOn(conn.processStderr(stream, nullptr));
        ASSERT_FALSE(error.e);
    }

    ASSERT_EQ(capture.starts.size(), 1u);
    EXPECT_EQ(capture.starts[0].type, actBuild);
    EXPECT_EQ(capture.starts[0].text, "building 'probe'");

    EXPECT_TRUE(capture.stops.empty());
    EXPECT_EQ(conn.remoteActivities.size(), 1u);

    // 2nd operation: result + stop.
    {
        StringSink sink;
        sink << uint64_t(STDERR_RESULT) << ActivityId(42) << uint64_t(resBuildLogLine);
        writeFields(sink, Logger::Fields{std::string("hello from the builder")});
        sink << uint64_t(STDERR_STOP_ACTIVITY) << ActivityId(42);
        writeLast(sink);
        writeFull(sockets.b.get(), sink.s);
    }
    {
        AsyncFdIoStream stream{AsyncFdIoStream::shared_fd{}, conn.getFD()};
        auto error = aio.blockOn(conn.processStderr(stream, nullptr));
        ASSERT_FALSE(error.e);
    }

    EXPECT_TRUE(conn.remoteActivities.empty());

    for (auto & message : capture.messages) {
        EXPECT_EQ(message.find("isn't currently known"), std::string::npos) << message;
    }

    EXPECT_EQ(capture.results, (std::vector<ResultType>{resBuildLogLine}));
    EXPECT_EQ(capture.buildLogLines, (std::vector<std::string>{"hello from the builder"}));

    // NOTE: the activity ID is the local one, not the "fake daemon" wire ID.
    // It must match whatever `startActivityImpl` provided.
    ASSERT_EQ(capture.stops.size(), 1u);
    EXPECT_EQ(capture.stops[0], capture.starts[0].id);
}

}
