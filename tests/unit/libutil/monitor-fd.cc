#include "lix/libutil/monitor-fd.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/file-descriptor.hh"
#include <atomic>
#include <future>
#include <gtest/gtest.h>
#include <sys/socket.h>

using namespace std::literals::chrono_literals;

namespace nix {

TEST(MonitorFdHup, works)
{
    int socks[2];
    int rv = socketpair(AF_UNIX, SOCK_STREAM, 0, socks);
    if (rv) throw SysError("socketpair");
    auto sock1 = AutoCloseFD{socks[0]};
    auto sock2 = AutoCloseFD{socks[1]};

    std::promise<void> called;

    MonitorFdHup monitor(sock1.get(), [&called]() {
        called.set_value();
    });

    sock2.close();

    // 30 seconds should certainly do it.
    called.get_future().wait_for(10s);
}

// Ensures that destroying the MonitorFdHup causes it to actually go away.
TEST(MonitorFdHup, destroys_safely)
{
    int socks[2];
    int rv = socketpair(AF_UNIX, SOCK_STREAM, 0, socks);
    if (rv) throw SysError("socketpair");
    auto sock1 = AutoCloseFD{socks[0]};
    auto sock2 = AutoCloseFD{socks[1]};

    {
        MonitorFdHup monitor(sock1.get(), []() {
            abort();
        });
    }
}

}
