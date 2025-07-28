#include "lix/libstore/filetransfer.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/compression.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/signals.hh"
#include "lix/libutil/thread-name.hh"

#include <cstdint>
#include <exception>
#include <future>
#include <gtest/gtest.h>
#include <kj/common.h>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <sys/poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

// local server tests don't work on darwin without some incantations
// the horrors do not want to look up. contributions welcome though!
#if __APPLE__
#define NOT_ON_DARWIN(n) DISABLED_##n
#else
#define NOT_ON_DARWIN(n) n
#endif

using namespace std::chrono_literals;

namespace {

struct Reply {
    std::string status, headers;
    std::function<std::optional<std::string>(int)> content;
    std::list<std::string> expectedHeaders;

    Reply(
        std::string_view status,
        std::string_view headers,
        std::function<std::string()> content,
        std::list<std::string> expectedHeaders = {}
    )
        : Reply(
            status,
            headers,
            [content](int round) { return round == 0 ? std::optional(content()) : std::nullopt; },
            std::move(expectedHeaders)
        )
    {
    }

    Reply(
        std::string_view status,
        std::string_view headers,
        std::function<std::optional<std::string>(int)> content,
        std::list<std::string> expectedHeaders = {}
    )
        : status(status)
        , headers(headers)
        , content(content)
        , expectedHeaders(std::move(expectedHeaders))
    {
    }
};
}

namespace nix {

static std::tuple<uint16_t, AutoCloseFD>
serveHTTP(std::vector<Reply> replies)
{
    AutoCloseFD listener(::socket(AF_INET6, SOCK_STREAM, 0));
    if (!listener) {
        throw SysError(errno, "socket() failed");
    }

    Pipe trigger;
    trigger.create();

    sockaddr_in6 addr = {
        .sin6_family = AF_INET6,
        .sin6_addr = IN6ADDR_LOOPBACK_INIT,
    };
    socklen_t len = sizeof(addr);
    if (::bind(listener.get(), reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) < 0) {
        throw SysError(errno, "bind() failed");
    }
    if (::getsockname(listener.get(), reinterpret_cast<sockaddr *>(&addr), &len) < 0) {
        throw SysError(errno, "getsockname() failed");
    }
    if (::listen(listener.get(), 1) < 0) {
        throw SysError(errno, "listen() failed");
    }

    std::thread(
        [replies, at{0}](AutoCloseFD socket, AutoCloseFD trigger) mutable {
            setCurrentThreadName("test httpd server");
            while (true) {
                pollfd pfds[2] = {
                    {
                        .fd = socket.get(),
                        .events = POLLIN,
                    },
                    {
                        .fd = trigger.get(),
                        .events = POLLHUP,
                    },
                };

                if (::poll(pfds, 2, -1) <= 0) {
                    throw SysError(errno, "poll() failed");
                }
                if (pfds[1].revents & POLLHUP) {
                    return;
                }
                if (!(pfds[0].revents & POLLIN)) {
                    continue;
                }

                AutoCloseFD conn(::accept(socket.get(), nullptr, nullptr));
                if (!conn) {
                    throw SysError(errno, "accept() failed");
                }

                const auto & reply = replies[at++ % replies.size()];

                std::thread([=, conn{std::move(conn)}] {
                    setCurrentThreadName("test httpd connection");
                    auto send = [&](std::string_view bit) {
                        while (!bit.empty()) {
                            auto written = ::send(conn.get(), bit.data(), bit.size(), MSG_NOSIGNAL);
                            if (written < 0) {
                                debug("send() failed: %s", strerror(errno));
                                return;
                            }
                            bit.remove_prefix(written);
                        }
                    };

                    send("HTTP/1.1 ");
                    send(reply.status);
                    send("\r\n");

                    std::string requestWithHeaders;
                    while (true) {
                        char c;
                        if (recv(conn.get(), &c, 1, MSG_NOSIGNAL) != 1) {
                            debug("recv() failed for headers: %s", strerror(errno));
                            return;
                        }
                        requestWithHeaders += c;
                        if (requestWithHeaders.ends_with("\r\n\r\n")) {
                            requestWithHeaders.resize(requestWithHeaders.size() - 2);
                            break;
                        }
                    }
                    debug("got request:\n%s", requestWithHeaders);
                    for (auto & expected : reply.expectedHeaders) {
                        ASSERT_TRUE(requestWithHeaders.contains(fmt("%s\r\n", expected)));
                    }

                    send(reply.headers);
                    send("\r\n");
                    for (int round = 0; ; round++) {
                        if (auto content = reply.content(round); content.has_value()) {
                            send(*content);
                        } else {
                            break;
                        }
                    }
                    ::shutdown(conn.get(), SHUT_WR);
                    for (;;) {
                        char buf[1];
                        switch (recv(conn.get(), buf, 1, MSG_NOSIGNAL)) {
                        case 0:
                            return; // remote closed
                        case 1:
                            continue; // connection still held open by remote
                        default:
                            debug("recv() failed: %s", strerror(errno));
                            return;
                        }
                    }
                }).detach();
            }
        },
        std::move(listener),
        std::move(trigger.readSide)
    )
        .detach();

    return {
        ntohs(addr.sin6_port),
        std::move(trigger.writeSide),
    };
}

static std::tuple<uint16_t, AutoCloseFD>
serveHTTP(std::string status, std::string headers, std::function<std::string()> content)
{
    return serveHTTP({{{status, headers, content}}});
}

TEST(FileTransfer, destructionAbortsDownload)
{
    AsyncIoRoot aio;
    auto ft = makeFileTransfer();

    auto [port, srv] = serveHTTP({{"200 ok", "", [](int) { return "foo"; }}});
    // discard the download stream. this must cancel the download, even when the
    // remote still has data to send. we simulate this by sending the same block
    // of data over and over without any content-length headers sent the client.
    (void) aio.blockOn(ft->download(fmt("http://[::1]:%d/index", port)));

    // makeFileTransfer returns a ref<>, which cannot be cleared. since we also
    // can't default-construct it we'll have to overwrite it instead, but we'll
    // take the raw pointer out first so we can destroy it in a detached thread
    // (otherwise a failure will stall the process and have it killed by meson)
    auto reset = std::async(std::launch::async, [&]() { ft = makeFileTransfer(); });
    EXPECT_EQ(reset.wait_for(10s), std::future_status::ready);
    // if this did time out we have to leak `reset`.
    if (reset.wait_for(0s) == std::future_status::timeout) {
        (void) new auto(std::move(reset));
    }
}

TEST(FileTransfer, exceptionAbortsRead)
{
    auto [port, srv] = serveHTTP("200 ok", "content-length: 0\r\n", [] { return ""; });
    AsyncIoRoot aio;
    auto ft = makeFileTransfer();
    char buf[10] = "";
    ASSERT_EQ(
        aio.blockOn(
            aio.blockOn(ft->download(fmt("http://[::1]:%d/index", port))).second->read(buf, 10)
        ),
        std::nullopt
    );
}

TEST(FileTransfer, NOT_ON_DARWIN(reportsSetupErrors))
{
    auto [port, srv] = serveHTTP("404 not found", "", [] { return ""; });
    AsyncIoRoot aio;
    auto ft = makeFileTransfer();
    ASSERT_THROW(aio.blockOn(ft->download(fmt("http://[::1]:%d/index", port))), FileTransferError);
}

TEST(FileTransfer, NOT_ON_DARWIN(defersFailures))
{
    auto [port, srv] = serveHTTP("200 ok", "content-length: 100000000\r\n", [] {
        std::this_thread::sleep_for(10ms);
        // just a bunch of data to fill the curl wrapper buffer, otherwise the
        // initial wait for header data will also wait for the the response to
        // complete (the source is only woken when curl returns data, and curl
        // might only do so once its internal buffer has already been filled.)
        return std::string(1024 * 1024, ' ');
    });
    AsyncIoRoot aio;
    auto ft = makeFileTransfer(0);
    auto src = aio.blockOn(ft->download(fmt("http://[::1]:%d/index", port))).second;
    ASSERT_THROW(aio.blockOn(src->drain()), FileTransferError);
}

TEST(FileTransfer, NOT_ON_DARWIN(handlesContentEncoding))
{
    std::string original = "Test data string";
    std::string compressed = compress("gzip", original);

    auto [port, srv] = serveHTTP("200 ok", "content-encoding: gzip\r\n", [&] { return compressed; });
    AsyncIoRoot aio;
    auto ft = makeFileTransfer();

    StringSink sink;
    aio.blockOn(
        aio.blockOn(ft->download(fmt("http://[::1]:%d/index", port))).second->drainInto(sink)
    );
    EXPECT_EQ(sink.s, original);
}

TEST(FileTransfer, usesIntermediateLinkHeaders)
{
    auto [port, srv] = serveHTTP({
        {"301 ok",
         "location: /second\r\n"
         "content-length: 0\r\n",
         [] { return ""; }},
        {"307 ok",
         "location: /third\r\n"
         "content-length: 0\r\n",
         [] { return ""; }},
        {"307 ok",
         "location: /fourth\r\n"
         "link: <http://foo>; rel=\"immutable\"\r\n"
         "content-length: 0\r\n",
         [] { return ""; }},
        {"200 ok", "content-length: 1\r\n", [] { return "a"; }},
    });
    AsyncIoRoot aio;
    auto ft = makeFileTransfer(0);
    auto [result, _data] = aio.blockOn(ft->download(fmt("http://[::1]:%d/first", port)));
    ASSERT_EQ(result.immutableUrl, "http://foo");
}

TEST(FileTransfer, stalledReaderDoesntBlockOthers)
{
    auto [port, srv] = serveHTTP({
        {"200 ok",
         "content-length: 100000000\r\n",
         [](int round) mutable {
             return round < 100 ? std::optional(std::string(1'000'000, ' ')) : std::nullopt;
         }},
    });
    AsyncIoRoot aio;
    auto ft = makeFileTransfer(0);
    auto [_result1, data1] = aio.blockOn(ft->download(fmt("http://[::1]:%d", port)));
    auto [_result2, data2] = aio.blockOn(ft->download(fmt("http://[::1]:%d", port)));
    auto drop = [&](AsyncInputStream & source, size_t size) {
        char buf[1000];
        size_t dropped = 0;
        while (size > 0) {
            auto round = std::min(size, sizeof(buf));
            auto got = aio.blockOn(source.read(buf, round));
            if (!got) {
                break;
            }
            round = *got;
            size -= round;
            dropped += round;
        }
        return dropped;
    };
    // read 10M of each of the 100M, then the rest. neither reader should
    // block the other, nor should it take that long to copy 200MB total.
    ASSERT_EQ(drop(*data1, 10'000'000), 10'000'000);
    ASSERT_EQ(drop(*data2, 10'000'000), 10'000'000);
    ASSERT_EQ(drop(*data1, 90'000'000), 90'000'000);
    ASSERT_EQ(drop(*data2, 90'000'000), 90'000'000);

    ASSERT_EQ(drop(*data1, 1), 0);
    ASSERT_EQ(drop(*data2, 1), 0);
}

TEST(FileTransfer, retries)
{
    auto [port, srv] = serveHTTP({
        // transient setup failure
        {"429 try again later", "content-length: 0\r\n", [] { return ""; }},
        // transient transfer failure (simulates a connection break)
        {"200 ok",
         "content-length: 2\r\n"
         "accept-ranges: bytes\r\n",
         [] { return "a"; }},
        // wrapper should ask for remaining data now
        {"200 ok",
         "content-length: 1\r\n"
         "content-range: bytes 1-1/2\r\n",
         [] { return "b"; },
         {"Range: bytes=1-"}},
    });
    AsyncIoRoot aio;
    auto ft = makeFileTransfer(0);
    auto [result, data] = aio.blockOn(ft->download(fmt("http://[::1]:%d", port)));
    ASSERT_EQ(aio.blockOn(data->drain()), "ab");
}

TEST(FileTransfer, doesntRetrySetupForever)
{
    auto [port, srv] = serveHTTP({
        {"429 try again later", "content-length: 0\r\n", [] { return ""; }},
    });
    AsyncIoRoot aio;
    auto ft = makeFileTransfer(0);
    ASSERT_THROW(aio.blockOn(ft->download(fmt("http://[::1]:%d", port))), FileTransferError);
}

TEST(FileTransfer, doesntRetryTransferForever)
{
    constexpr size_t LIMIT = 20;
    ASSERT_LT(fileTransferSettings.tries, LIMIT); // just to keep test runtime low
    std::vector<Reply> replies;
    for (size_t i = 0; i < LIMIT; i++) {
        replies.emplace_back(
            "200 ok",
            fmt("content-length: %1%\r\n"
                "accept-ranges: bytes\r\n"
                "content-range: bytes %2%-%3%/%3%\r\n",
                LIMIT - i,
                i,
                LIMIT),
            [] { return "a"; }
        );
    }
    auto [port, srv] = serveHTTP(replies);
    AsyncIoRoot aio;
    auto ft = makeFileTransfer(0);
    ASSERT_THROW(
        aio.blockOn(aio.blockOn(ft->download(fmt("http://[::1]:%d", port))).second->drain()),
        FileTransferError
    );
}

TEST(FileTransfer, doesntRetryUploads)
{
    AsyncIoRoot aio;
    auto ft = makeFileTransfer(0);

    {
        auto [port, srv] = serveHTTP({
            {"429 try again later", "", [] { return ""; }},
            {"200 ok", "", [] { return ""; }},
        });
        ASSERT_THROW(aio.blockOn(ft->upload(fmt("http://[::1]:%d", port), "")), FileTransferError);
    }
    {
        auto [port, srv] = serveHTTP({
            {"429 try again later", "", [] { return ""; }},
            {"200 ok", "", [] { return ""; }},
        });
        ASSERT_THROW(
            aio.blockOn(ft->upload(fmt("http://[::1]:%d", port), "foo")), FileTransferError
        );
    }
}

// this test does not work unless run alone. we can't fork because that breaks
// the file transfer thread, restoring state is insufficient and very fragile.
TEST(FileTransfer, DISABLED_interrupt)
{
    struct InterruptingLogger : Logger
    {
        void log(Verbosity lvl, std::string_view s) override
        {
            if (s.starts_with("finished") && s.ends_with("body = 10 bytes")) {
                triggerInterrupt();
                checkInterrupt();
            }
        }
        void logEI(const ErrorInfo & ei) override
        {
        }
    };

    verbosity = lvlDebug;
    logger = new InterruptingLogger;

    AsyncIoRoot aio;
    auto ft = makeFileTransfer(0);
    auto [port, srv] = serveHTTP({
        {"200 ok", "content-length: 10\r\n", [] { return "0123456789"; }},
    });

    ASSERT_THROW(
        aio.blockOn(aio.blockOn(ft->download(fmt("http://[::1]:%d/index", port))).second->drain()),
        FileTransferError
    );
}

TEST(FileTransfer, setupErrorsAreMetadata)
{
    auto [port, srv] = serveHTTP({
        {"404 try again later", "content-length: 1\r\n", [] { return "X"; }},
    });
    AsyncIoRoot aio;
    auto ft = makeFileTransfer(0);
    ASSERT_THROW(aio.blockOn(ft->upload(fmt("http://[::1]:%d", port), "")), FileTransferError);
}

}
