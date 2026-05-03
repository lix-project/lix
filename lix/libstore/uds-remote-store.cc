#include "lix/libstore/uds-remote-store.hh"
#include "globals.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/strings.hh"
#include "lix/libutil/unix-domain-socket.hh"
#include "lix/libstore/worker-protocol.hh"

#include <algorithm>
#include <cerrno>
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
        return fmt(
            "unix://%s%s", *path, config().protocol.overridden ? fmt("?protocol=%s", config().protocol) : ""
        );
    } else {
        return "daemon";
    }
}

static bool tryToConnect(AutoCloseFD & sockFD, const daemon::Protocol & socket)
{
    try {
        nix::connect(sockFD.get(), socket.path);
        return true;
    } catch (SysError & e) {
        if (e.errNo == EACCES || e.errNo == EPERM || e.errNo == ECONNREFUSED || e.errNo == ENOENT
            || e.errNo == ENOTDIR || e.errNo == ENOTSOCK)
        {
            debug("skipping socket %s: %s", socket.path, strerror(e.errNo));
            return false;
        } else {
            throw;
        }
    }
}

kj::Promise<Result<ref<RemoteStore::Connection>>> UDSRemoteStore::openConnection()
try {
    auto conn = make_ref<Connection>();

    /* Connect to a daemon that does the privileged work for us. */
    conn->fd = createUnixDomainSocket();

    std::list<daemon::Protocol> candidates;

    if (path) {
        if (config().protocol == "any") {
            candidates = daemon::supportedProtocols(*path);
        } else {
            for (const auto & proto : tokenizeString<std::list<std::string>>(config().protocol.get(), " ,")) {
                candidates.push_back(daemon::getProtocol(proto, *path));
            }
        }
    } else {
        if (config().protocol == "any") {
            candidates = settings.nixDaemonSockets();
        } else {
            for (const auto & proto : tokenizeString<std::list<std::string>>(config().protocol.get(), " ,")) {
                auto socket = daemon::getProtocol(proto);
                for (auto & candidate : settings.nixDaemonSockets()) {
                    // legacy-combined has even more special socket search behavior for `daemon` uris. sigh.
                    if (candidate.type == socket.type
                        || (socket.type == daemon::Protocol::LEGACY_COMBINED
                            && candidate.type == daemon::Protocol::LEGACY))
                    {
                        candidates.push_back(candidate);
                    }
                }
            }
        }
    }

    for (const auto & path : candidates) {
        if (tryToConnect(conn->fd, path)) {
            conn->startTime = std::chrono::steady_clock::now();
            co_return conn;
        }
    }

    throw Error(
        "could not connect to any lix socket (tried %s)",
        concatMapStringsSep(", ", candidates, [](auto & s) { return s.path; })
    );
} catch (...) {
    co_return result::current_exception();
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
