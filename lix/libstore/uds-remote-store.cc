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

namespace {
MakeError(ProtocolUnavailable, Error);
}

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

RpcRemoteStore::RpcRemoteStore(
    MustCallInit & w, Badge b, UDSRemoteStoreConfig config, std::optional<std::string> path
)
    : Store(config)
    , RemoteStore(w, config)
    , UDSRemoteStore(w, b, config, path)
{
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

kj::Promise<Result<std::shared_ptr<StoreProxy>>>
UDSRemoteStore::proxy(UDSRemoteStoreConfig config, std::optional<std::string> path)
try {
    struct Proxy : StoreProxy
    {
        AutoCloseFD fd;

        Proxy(AutoCloseFD fd) : fd(std::move(fd)) {}

        int getFD() const override
        {
            return fd.get();
        }
    };

    auto candidates = protocolsFor(path, config.protocol.get());
    // we only support proxying legacy wire connections for now. rpc connections should
    // use a new scheme instead since passing protocol types for remote store urls is a
    // right pain in the tail (and might even break on hardened remote builder setups).
    candidates.remove_if([](daemon::Protocol & p) {
        return !(p.type == daemon::Protocol::LEGACY_COMBINED || p.type == daemon::Protocol::LEGACY);
    });

    for (const auto & path : candidates) {
        auto fd = createUnixDomainSocket();
        if (tryToConnect(fd, path)) {
            co_return std::make_shared<Proxy>(std::move(fd));
        }
    }

    throw Error(
        "could not connect to any lix socket (tried %s)",
        concatMapStringsSep(", ", candidates, [](auto & s) { return s.path; })
    );
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<std::optional<ref<Store>>>>
UDSRemoteStore::open(UDSRemoteStoreConfig config, std::optional<std::string> path)
try {
    std::list<daemon::Protocol> candidates = protocolsFor(path, config.protocol.get());

    for (const auto & socketPath : candidates) {
        auto fd = createUnixDomainSocket();

        if (tryToConnect(fd, socketPath)) {
            try {
                switch (socketPath.type) {
                case daemon::Protocol::LEGACY_COMBINED:
                case daemon::Protocol::LEGACY: {
                    MustCallInit init;
                    auto store = make_ref<UDSRemoteStore>(init, Badge{}, config, path);
                    TRY_AWAIT(init(store, std::move(fd)));
                    co_return store;
                }

                case daemon::Protocol::RPC_V1: {
                    MustCallInit init;
                    auto store = make_ref<RpcRemoteStore>(init, Badge{}, config, path);
                    TRY_AWAIT(init(store, std::move(fd)));
                    co_return store;
                }
                }
            } catch (ProtocolUnavailable &) {
            }
        }
    }

    throw Error(
        "could not connect to any lix socket (tried %s)",
        concatMapStringsSep(", ", candidates, [](auto & s) { return s.path; })
    );
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> UDSRemoteStore::init(AutoCloseFD fd)
try {
    auto conn = std::make_shared<Connection>();
    conn->fd = std::move(fd);
    conn->startTime = std::chrono::steady_clock::now();
    TRY_AWAIT(RemoteStore::initConnection(*conn));
    *(co_await connection.lock()) = conn;
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<void>> RpcRemoteStore::init(AutoCloseFD fd)
try {
    auto conn = std::make_shared<Connection>();
    conn->fd = std::move(fd);
    conn->startTime = std::chrono::steady_clock::now();
    if (!TRY_AWAIT(prepareRpcConnection(*conn))) {
        throw ProtocolUnavailable("");
    }
    TRY_AWAIT(setOptions(*conn));
    *(co_await connection.lock()) = conn;
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<bool>> RpcRemoteStore::prepareRpcConnection(Connection & con)
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

    rpc = std::make_shared<RpcState>(RpcState{
        .rpcStream = std::move(rpcStream),
        .proxySock = make_box_ptr<AsyncFdIoStream>(std::move(proxyAsync)),
        .client = std::move(client),
        .loggerActivity = logger->startActivity(lvlDebug, actUnknown, "daemon connection"),
        .legacyProtocol = nullptr,
        .requestStream = nullptr,
        .forwarder = nullptr,
    });

    auto bootstrapReq = bootstrap.requestRequest();
    bootstrapReq.setClientInfo(PACKAGE_STRING);
    RPC_FILL(bootstrapReq, setProtocol, rpc::daemon::UNSTABLE_LEGACY_TUNNELED);
    auto legacyBoot =
        TRY_AWAIT_RPC_NOEXCEPT(bootstrapReq.send()).getResult().castAs<rpc::daemon::LegacyBoot>();
    auto initReq = legacyBoot.initRequest();
    initReq.setLogger(kj::heap<rpc::log::RpcLoggerServer>(rpc->loggerActivity));
    initReq.setReplyStream(kj::heap<LegacyStreamProxy>(*rpc));

    auto initResp = TRY_AWAIT_RPC(initReq.send());
    auto initResult = initResp.getResult();
    con.remoteTrustsUs = initResult.getTrust() == rpc::daemon::LegacyBoot::Trust::TRUSTED
        ? std::optional{Trusted}
        : initResult.getTrust() == rpc::daemon::LegacyBoot::Trust::UNTRUSTED ? std::optional{NotTrusted}
                                                                             : std::nullopt;
    con.daemonVersion = PROTOCOL_VERSION;
    con.daemonNixVersion = rpc::to<std::string>(initResult.getVersion());
    con.store = this;
    rpc->legacyProtocol = initResult.getProtocol();
    rpc->requestStream = initResult.getRequestStream();
    rpc->forwarder = rpc->forwardRequests();

    co_return true;
} catch (std::exception & e) { // NOLINT(lix-foreign-exceptions): just fall back to legacy for now
    debug("rpc connection failed: %s", e.what());
    co_return false;
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<void> RpcRemoteStore::RpcState::forwardRequests()
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

kj::Promise<void> RpcRemoteStore::LegacyStreamProxy::feed(FeedContext context)
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

kj::Promise<void> RpcRemoteStore::LegacyStreamProxy::sync(SyncContext context)
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

/* Overrides for RPC-aware versions of RemoteStore commands */

kj::Promise<Result<void>> RpcRemoteStore::optimiseStore()
try {
    TRY_AWAIT_RPC(rpc->legacyProtocol.optimiseStoreRequest().send());
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}
}
