#include "filetransfer.hh"
#include "compression.hh"

#include <cstdint>
#include <exception>
#include <future>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <stdexcept>
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

namespace nix {

static std::tuple<uint16_t, AutoCloseFD>
serveHTTP(std::string_view status, std::string_view headers, std::function<std::string()> content)
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
        [status, headers, content](AutoCloseFD socket, AutoCloseFD trigger) {
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

                auto send = [&](std::string_view bit) {
                    while (!bit.empty()) {
                        auto written = ::write(conn.get(), bit.data(), bit.size());
                        if (written < 0) {
                            throw SysError(errno, "write() failed");
                        }
                        bit.remove_prefix(written);
                    }
                };

                send("HTTP/1.1 ");
                send(status);
                send("\r\n");
                send(headers);
                send("\r\n");
                send(content());
                ::shutdown(conn.get(), SHUT_RDWR);
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

TEST(FileTransfer, exceptionAbortsDownload)
{
    struct Done
    {};

    auto ft = makeFileTransfer();

    LambdaSink broken([](auto block) { throw Done(); });

    ASSERT_THROW(ft->download(FileTransferRequest("file:///dev/zero"), broken), Done);

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

TEST(FileTransfer, NOT_ON_DARWIN(reportsSetupErrors))
{
    auto [port, srv] = serveHTTP("404 not found", "", [] { return ""; });
    auto ft = makeFileTransfer();
    ASSERT_THROW(
        ft->download(FileTransferRequest(fmt("http://[::1]:%d/index", port))),
        FileTransferError);
}

TEST(FileTransfer, NOT_ON_DARWIN(reportsTransferError))
{
    auto [port, srv] = serveHTTP("200 ok", "content-length: 100\r\n", [] {
        std::this_thread::sleep_for(10ms);
        return "";
    });
    auto ft = makeFileTransfer();
    FileTransferRequest req(fmt("http://[::1]:%d/index", port));
    req.baseRetryTimeMs = 0;
    ASSERT_THROW(ft->download(req), FileTransferError);
}

TEST(FileTransfer, NOT_ON_DARWIN(handlesContentEncoding))
{
    std::string original = "Test data string";
    std::string compressed = compress("gzip", original);

    auto [port, srv] = serveHTTP("200 ok", "content-encoding: gzip\r\n", [&] { return compressed; });
    auto ft = makeFileTransfer();

    StringSink sink;
    ft->download(FileTransferRequest(fmt("http://[::1]:%d/index", port)), sink);
    EXPECT_EQ(sink.s, original);
}
}
