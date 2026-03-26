#include "common.hh"
#include <sys/socket.h>
#include <sys/un.h>

LIBEXEC_HELPER(5)

static int resultFd = -1;

static void sendResult(int result)
{
    // NOTE posix says pipe writes smaller than PIPE_BUF must be atomic, so this either
    // succeeds or fails (pipe bufs of four bytes make no sense at all for our systems)
    int error = result;
    DIE_UNLESS_SYS("writing result", ::write(resultFd, &error, sizeof(error)));
}

int helperMain(const char *, std::span<char *> args) noexcept
{
    int socket = argToInt<int>("socket", args[0]);
    std::string_view method = args[1];
    const auto dir = args[2];
    const auto name = args[3];
    resultFd = argToInt<int>("result-fd", args[4]);

    if (chdir(dir)) {
        sendResult(errno);
        return 0;
    }

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;

    if (auto nameLen = strlen(name); nameLen + 1 >= sizeof(addr.sun_path)) {
        die(std::format("socket path {}/{} is too long", dir, name));
    } else {
        memcpy(addr.sun_path, name, nameLen + 1);
    }

    if (method == "bind") {
        auto result = bind(socket, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) ? errno : 0;
        sendResult(result);
    } else if (method == "connect") {
        auto result = connect(socket, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) ? errno : 0;
        sendResult(result);
    } else {
        die(std::format("invalid method %s", method));
    }

    return 0;
}
