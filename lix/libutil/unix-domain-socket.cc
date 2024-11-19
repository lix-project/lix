#include "lix/libutil/file-system.hh"
#include "lix/libutil/processes.hh"
#include "lix/libutil/unix-domain-socket.hh"
#include "lix/libutil/strings.hh"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace nix {

AutoCloseFD createUnixDomainSocket()
{
    AutoCloseFD fdSocket{socket(PF_UNIX, SOCK_STREAM
        #ifdef SOCK_CLOEXEC
        | SOCK_CLOEXEC
        #endif
        , 0)};
    if (!fdSocket)
        throw SysError("cannot create Unix domain socket");
    closeOnExec(fdSocket.get());
    return fdSocket;
}


AutoCloseFD createUnixDomainSocket(const Path & path, mode_t mode)
{
    auto fdSocket = nix::createUnixDomainSocket();

    bind(fdSocket.get(), path);

    chmodPath(path.c_str(), mode);

    if (listen(fdSocket.get(), 100) == -1)
        throw SysError("cannot listen on socket '%1%'", path);

    return fdSocket;
}

/**
 * Workaround for the max length of Unix socket names being between 102
 * (darwin) and 108 (Linux), which is extremely short. This limitation is
 * caused by historical restrictions on sizeof(struct sockaddr):
 * https://unix.stackexchange.com/a/367012.
 *
 * Our solution here is to start a process inheriting the socket, chdir into
 * the directory of the socket, then connect with just the filename. This is
 * rather silly but it works around working directory being process-wide state,
 * and is as clearly sound as possible.
 */
static void bindConnectProcHelper(
    std::string_view operationName, auto && operation,
    int fd, const std::string & path)
{
    struct sockaddr_un addr;
    addr.sun_family = AF_UNIX;

    // Casting between types like these legacy C library interfaces
    // require is forbidden in C++. To maintain backwards
    // compatibility, the implementation of the bind/connect functions
    // contains some hints to the compiler that allow for this
    // special case.
    auto * psaddr = reinterpret_cast<struct sockaddr *>(&addr);

    if (path.size() + 1 >= sizeof(addr.sun_path)) {
        Pipe pipe;
        pipe.create();
        Pid pid{startProcess([&] {
            try {
                pipe.readSide.close();
                Path dir = dirOf(path);
                if (chdir(dir.c_str()) == -1)
                    throw SysError("chdir to '%s' failed", dir);
                std::string base(baseNameOf(path));
                if (base.size() + 1 >= sizeof(addr.sun_path))
                    throw Error("socket path '%s' is too long", base);
                memcpy(addr.sun_path, base.c_str(), base.size() + 1);
                if (operation(fd, psaddr, sizeof(addr)) == -1)
                    throw SysError("cannot %s to socket at '%s'", operationName, path);
                writeFull(pipe.writeSide.get(), "0\n");
            } catch (SysError & e) {
                writeFull(pipe.writeSide.get(), fmt("%d\n", e.errNo));
            } catch (...) {
                writeFull(pipe.writeSide.get(), "-1\n");
            }
        })};
        pipe.writeSide.close();
        auto errNo = string2Int<int>(chomp(drainFD(pipe.readSide.get())));
        if (!errNo || *errNo == -1)
            throw Error("cannot %s to socket at '%s'", operationName, path);
        else if (*errNo > 0) {
            errno = *errNo;
            throw SysError("cannot %s to socket at '%s'", operationName, path);
        }
    } else {
        memcpy(addr.sun_path, path.c_str(), path.size() + 1);
        if (operation(fd, psaddr, sizeof(addr)) == -1)
            throw SysError("cannot %s to socket at '%s'", operationName, path);
    }
}


void bind(int fd, const std::string & path)
{
    unlink(path.c_str());

    bindConnectProcHelper("bind", ::bind, fd, path);
}


void connect(int fd, const std::string & path)
{
    bindConnectProcHelper("connect", ::connect, fd, path);
}

}
