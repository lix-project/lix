#include "lix/libstore/uds-remote-store.hh"
#include "globals.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/strings.hh"
#include "lix/libutil/unix-domain-socket.hh"
#include "lix/libstore/worker-protocol.hh"

#include <cerrno>
#include <ranges>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstring>


namespace nix {

std::string UDSRemoteStoreConfig::doc()
{
    return
        #include "uds-remote-store.md"
        ;
}

UDSRemoteStore::UDSRemoteStore(UDSRemoteStoreConfig config)
    : Store(config)
    , RemoteStore(config)
    , config_(std::move(config))
{
}


UDSRemoteStore::UDSRemoteStore(
    const std::string scheme,
    std::string socket_path,
    UDSRemoteStoreConfig config)
    : UDSRemoteStore(std::move(config))
{
    path.emplace(socket_path);
}


std::string UDSRemoteStore::getUri()
{
    if (path) {
        return std::string("unix://") + *path;
    } else {
        return "daemon";
    }
}

static void connectToFirstAvailableSocket(AutoCloseFD & sockFD, const std::list<Path> & paths)
{
    for (const auto & socket : paths) {
        try {
            nix::connect(sockFD.get(), socket);
            return;
        } catch (SysError & e) {
            if (e.errNo == EACCES || e.errNo == EPERM || e.errNo == ECONNREFUSED || e.errNo == ENOENT) {
                debug("skipping socket %s: %s", socket, strerror(e.errNo));
            } else {
                throw;
            }
        }
    }
    throw Error("could not connect to any lix socket (tried %s)", concatStringsSep(", ", paths));
}

ref<RemoteStore::Connection> UDSRemoteStore::openConnection()
{
    auto conn = make_ref<Connection>();

    /* Connect to a daemon that does the privileged work for us. */
    conn->fd = createUnixDomainSocket();

    std::list<Path> candidates;

    if (path) {
        candidates.emplace_back(*path);
    } else {
        candidates = settings.nixDaemonSockets()
            | std::views::transform([](auto & socket) { return socket.path; })
            | std::ranges::to<std::list<Path>>();
    }

    connectToFirstAvailableSocket(conn->fd, candidates);

    conn->startTime = std::chrono::steady_clock::now();

    return conn;
}


kj::Promise<Result<void>> UDSRemoteStore::addIndirectRoot(const Path & path)
try {
    auto conn(TRY_AWAIT(getConnection()));
    TRY_AWAIT(conn.sendCommand<unsigned>(WorkerProto::Op::AddIndirectRoot, path));
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}


void registerUDSRemoteStore() {
    StoreImplementations::add<UDSRemoteStore, UDSRemoteStoreConfig>();
}

}
