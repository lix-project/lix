#include "common.hh"
#include <sys/socket.h>
#include <sys/un.h>

LIBEXEC_HELPER(4)

int helperMain(const char *, std::span<char *> args) noexcept
{
    int socket = argToInt<int>("socket", args[0]);
    std::string_view method = args[1];
    const auto dir = args[2];
    const auto name = args[3];

    DIE_UNLESS_SYS("chdir", chdir(dir));

    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;

    if (auto nameLen = strlen(name); nameLen + 1 >= sizeof(addr.sun_path)) {
        die(std::format("socket path {}/{} is too long", dir, name));
    } else {
        memcpy(addr.sun_path, name, nameLen + 1);
    }

    if (method == "bind") {
        DIE_UNLESS_SYS("bind", bind(socket, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)));
    } else if (method == "connect") {
        DIE_UNLESS_SYS("connect", connect(socket, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)));
    } else {
        die(std::format("invalid method %s", method));
    }

    return 0;
}
