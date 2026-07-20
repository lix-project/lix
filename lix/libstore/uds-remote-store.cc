#include "lix/libstore/uds-remote-store.hh"
#include "daemon.capnp.h"
#include "globals.hh"
#include "libstore/daemon.hh"
#include "libutil/logging-rpc.hh"
#include "libutil/rpc.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/rpc.hh"
#include "lix/libutil/types-rpc.hh"
#include "lix/libutil/strings.hh"
#include "lix/libutil/unix-domain-socket.hh"
#include "lix/libstore/worker-protocol.hh"

#include <algorithm>
#include <capnp/rpc-twoparty.h>
#include <cerrno>
#include <exception>
#include <kj/async.h>
#include <kj/encoding.h>
#include <string_view>
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

UDSRemoteStore::UDSRemoteStore(
    MustCallInit & w, Badge, UDSRemoteStoreConfig config, std::optional<std::string> path
)
    : Store(config)
    , RemoteStore(w, config)
    , config_(std::move(config))
    , path(std::move(path))
{
}

kj::Promise<Result<std::optional<ref<Store>>>>
UDSRemoteStore::open(UDSRemoteStoreConfig config, std::optional<std::string> path)
try {
    MustCallInit init;
    auto store = make_ref<UDSRemoteStore>(init, Badge{}, std::move(config), std::move(path));
    TRY_AWAIT(init(store));
    co_return store;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::shared_ptr<StoreProxy>>>
UDSRemoteStore::proxy(UDSRemoteStoreConfig config, std::optional<std::string> path)
try {
    struct Proxy : StoreProxy
    {
        std::shared_ptr<RemoteStore::Connection> inner;

        Proxy(std::shared_ptr<RemoteStore::Connection> inner) : inner(inner) {}

        int getFD() const override
        {
            return inner->getFD();
        }
    };

    auto conn = TRY_AWAIT(openConnection(path, config.protocol.get(), false));
    co_return std::make_shared<Proxy>(conn);
} catch (...) {
    co_return result::current_exception();
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

static std::list<daemon::Protocol>
protocolsFor(const std::optional<std::string> & path, std::string_view protocol)
{
    std::list<daemon::Protocol> candidates;

    if (path) {
        if (protocol == "any") {
            candidates = daemon::supportedProtocols(*path);
        } else {
            for (const auto & proto : tokenizeString<std::list<std::string>>(protocol, " ,")) {
                candidates.push_back(daemon::getProtocol(proto, *path));
            }
        }
    } else {
        if (protocol == "any") {
            candidates = settings.nixDaemonSockets();
        } else {
            for (const auto & proto : tokenizeString<std::list<std::string>>(protocol, " ,")) {
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

    return candidates;
}

kj::Promise<Result<std::shared_ptr<UDSRemoteStore::Connection>>> UDSRemoteStore::openConnection(
    const std::optional<std::string> & path, std::string_view protocol, bool allowRPC, Store * rpcStore
)
try {
    auto conn = make_ref<Connection>();

    std::list<daemon::Protocol> candidates = protocolsFor(path, protocol);

    for (const auto & path : candidates) {
        if (!allowRPC || !rpcStore) {
            switch (path.type) {
            case daemon::Protocol::LEGACY_COMBINED:
            case daemon::Protocol::LEGACY:
                break;
            case daemon::Protocol::RPC_V1:
                continue;
            }
        }

        /* Connect to a daemon that does the privileged work for us. */
        conn->fd = createUnixDomainSocket();

        if (tryToConnect(conn->fd, path)) {
            conn->startTime = std::chrono::steady_clock::now();

            // NOTE we do all this setup *here* instead of in initConnection because we want to
            // provide graceful fallback during the transition period to rpc. since clients and
            // daemons can be updated independently we can never be sure that the rpc protocols
            // we want to use are supported on both sides without checking first, and sadly the
            // only place we can do such checks without disturbing fallback behavior is *here*.
            if (path.type == daemon::Protocol::RPC_V1 && !TRY_AWAIT(prepareRpcConnection(*conn, rpcStore))) {
                continue;
            }

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

kj::Promise<Result<void>> UDSRemoteStore::init()
try {
    auto conn = TRY_AWAIT(openConnection(path, config().protocol.get(), true, this));
    if (conn->rpc) {
        TRY_AWAIT(setOptions(*conn));
    } else {
        TRY_AWAIT(RemoteStore::initConnection(*conn));
    }
    *(co_await connection.lock()) = conn;
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<bool>> UDSRemoteStore::prepareRpcConnection(Connection & con, Store * store)
try {
    auto rpcStream =
        AIO().lowLevelProvider.wrapSocketFd(con.fd.get(), kj::LowLevelAsyncIoProvider::TAKE_OWNERSHIP);
    con.fd.release();
    auto [proxyAsync, proxySync] = SocketPair::stream();
    con.fd = std::move(proxySync);
    auto client = make_box_ptr<capnp::TwoPartyClient>(*rpcStream);
    auto bootstrap = client->bootstrap().castAs<rpc::daemon::Bootstrap>();

    // simplistic setup until we have a reasonable state to work with: talk only to
    // daemons that support the exact protocol list we support, including the uuid.
    {
        auto supported = co_await bootstrap.supportedRequest().send();
        auto supportedProtos = supported.getProtocols();
        debug("remote advertised %s", supported.toString().flatten().cStr());
        if (supportedProtos.size() != 1
            && rpc::to<std::string_view>(supportedProtos[0].getId()) != rpc::daemon::UNSTABLE_LEGACY_TUNNELED)
        {
            co_return false;
        }
    }

    con.rpc = std::make_shared<RpcState>(RpcState{
        .rpcStream = std::move(rpcStream),
        .proxySock = make_box_ptr<AsyncFdIoStream>(std::move(proxyAsync)),
        .client = std::move(client),
        .loggerActivity = logger->startActivity(lvlDebug, actUnknown, "daemon connection"),
        .requestStream = nullptr,
        .forwarder = nullptr,
    });

    auto bootstrapReq = bootstrap.requestRequest();
    bootstrapReq.setClientInfo(PACKAGE_STRING);
    RPC_FILL(bootstrapReq, setProtocol, rpc::daemon::UNSTABLE_LEGACY_TUNNELED);
    auto legacyBoot =
        TRY_AWAIT_RPC_NOEXCEPT(bootstrapReq.send()).getResult().castAs<rpc::daemon::LegacyBoot>();
    auto initReq = legacyBoot.initRequest();
    initReq.setLogger(kj::heap<rpc::log::RpcLoggerServer>(con.rpc->loggerActivity));
    initReq.setReplyStream(kj::heap<LegacyStreamProxy>(*con.rpc));

    auto initResp = TRY_AWAIT_RPC(initReq.send());
    auto initResult = initResp.getResult();
    con.remoteTrustsUs = initResult.getTrust() == rpc::daemon::LegacyBoot::Trust::TRUSTED
        ? std::optional{Trusted}
        : initResult.getTrust() == rpc::daemon::LegacyBoot::Trust::UNTRUSTED ? std::optional{NotTrusted}
                                                                             : std::nullopt;
    con.daemonVersion = PROTOCOL_VERSION;
    con.daemonNixVersion = rpc::to<std::string>(initResult.getVersion());
    con.store = store;
    con.rpc->requestStream = initResult.getRequestStream();
    con.rpc->forwarder = con.rpc->forwardRequests();

    co_return true;
} catch (std::exception & e) { // NOLINT(lix-foreign-exceptions): just fall back to legacy for now
    debug("rpc connection failed: %s", e.what());
    co_return false;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<void> UDSRemoteStore::RpcState::forwardRequests()
try {
    std::array<char, 8192> buf;
    while (true) {
        if (auto got = TRY_AWAIT(proxySock->read(buf.data(), buf.size())); !got) {
            break;
        } else {
            auto req = requestStream.feedRequest();
            req.initRaw(*got);
            std::copy(buf.begin(), buf.begin() + *got, req.getRaw().begin());
            TRY_AWAIT_RPC(req.send());
        }
    }
    TRY_AWAIT_RPC(requestStream.syncRequest().send());
} catch (...) {
    ignoreExceptionExceptInterrupt();
    error = std::current_exception();
}

kj::Promise<void> UDSRemoteStore::LegacyStreamProxy::feed(FeedContext context)
try {
    if (state.error) {
        std::rethrow_exception(state.error);
    }
    auto bytes = context.getParams().getRaw();
    TRY_AWAIT(state.proxySock->writeFull(bytes.begin(), bytes.size()));
} catch (...) {
    state.error = std::current_exception();
    rpc::rethrow_as_rpc_error();
}

kj::Promise<void> UDSRemoteStore::LegacyStreamProxy::sync(SyncContext context)
try {
    if (state.error) {
        std::rethrow_exception(state.error);
    }
    return kj::READY_NOW;
} catch (...) {
    rpc::rethrow_as_rpc_error();
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
    StoreImplementations::add<UDSRemoteStore, UDSRemoteStoreConfig>({"unix"});
}

}
