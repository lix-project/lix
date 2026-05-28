#pragma once
///@file

#include "lix/libstore/globals.hh"
#include "lix/libutil/file-descriptor.hh"
#include "lix/libutil/box_ptr.hh"
#include "lix/libutil/logging-rpc.hh"
#include "lix/libutil/logging.hh"
#include "lix/libstore/remote-store.hh"
#include "lix/libstore/remote-store-connection.hh"
#include "lix/libstore/indirect-root-store.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libstore/daemon.capnp.h"
#include <capnp/rpc-twoparty.h>
#include <kj/async-io.h>
#include <kj/async.h>
#include <memory>

namespace nix {

struct UDSRemoteStoreConfig : virtual LocalFSStoreConfig, virtual RemoteStoreConfig
{
    UDSRemoteStoreConfig(const Params & params)
        : StoreConfig(params)
        , LocalFSStoreConfig(params)
        , RemoteStoreConfig(params)
    {
    }

    const Setting<std::string> protocol{
        this,
        "legacy-combined",
        "protocol",
        R"(
          Space-or-comma-separated list of protocols to try to connect to, in preference order.
          Currently supported:
            - `legacy-combined` (default): legacy wire protocol using a single combined socket.
              The provided path will be used *unmodified* to locate the combined daemon socket.
            - `legacy`: legacy wire protocol using a single socket, but the path is used as the
              base directory for protocol-dependent socket lookup (appending `/socket` to path)
            - `lix-xp-1`: experimental RPC protocol. Will use the path as directory of sockets.

          Also supports the special value `any` to try *all* known protocols using the provided
          path as the *base* directory for sockets. Unlike `legacy-combined` this will append a
          `/socket` to the given path when trying to connect with the legacy-combined protocol.

          Ignored unless a path is also present.

          **NOTE**: for compatibility reasons `legacy-combined` uses the provided path as is to
          find its socket, all other protocols treat the path as a directory containing sockets
          for many protocols. As such `legacy-combined` can only be used alone, use `legacy` if
          you want to use the legacy wire protocol together with other, more modern, protocols.
        )"
    };

    const std::string name() override { return "Local Daemon Store"; }

    std::string doc() override;
};

class UDSRemoteStore : public virtual IndirectRootStore
    , public virtual RemoteStore
{
    friend MustCallInit;

    UDSRemoteStoreConfig config_;

protected:
    using Badge = kj::Badge<UDSRemoteStore>;

public:

    UDSRemoteStore(MustCallInit & w, Badge, UDSRemoteStoreConfig config, std::optional<std::string> path);

    static kj::Promise<Result<std::optional<ref<Store>>>>
    open(UDSRemoteStoreConfig config, std::optional<std::string> path);

    static kj::Promise<Result<std::optional<ref<Store>>>> open(UDSRemoteStoreConfig config)
    {
        return open(std::move(config), std::nullopt);
    }

    static kj::Promise<Result<std::optional<ref<Store>>>>
    open(const std::string & scheme, const Path & uri, UDSRemoteStoreConfig config)
    {
        return open(std::move(config), uri);
    }

    static kj::Promise<Result<std::shared_ptr<StoreProxy>>>
    proxy(UDSRemoteStoreConfig config, std::optional<std::string> path);

    UDSRemoteStoreConfig & config() override { return config_; }
    const UDSRemoteStoreConfig & config() const override { return config_; }

    std::string getUri() override;

    ref<FSAccessor> getFSAccessor() override
    { return LocalFSStore::getFSAccessor(); }

    kj::Promise<Result<box_ptr<AsyncInputStream>>>
    narFromPath(const StorePath & path, const Activity * context) override
    {
        return RemoteStore::narFromPath(path, context);
    }

    kj::Promise<Result<void>> repairPath(const StorePath & path) override
    try {
        unsupported(
            "repairPath",
            HintFmt("This command must be run as %s with %s", "root", "--store local").str()
        );
    } catch (...) {
        return {result::current_exception()};
    }

    /**
     * Implementation of `IndirectRootStore::addIndirectRoot()` which
     * delegates to the remote store.
     *
     * The idea is that the client makes the direct symlink, so it is
     * owned managed by the client's user account, and the server makes
     * the indirect symlink.
     */
    kj::Promise<Result<void>> addIndirectRoot(const Path & path) override;

private:
    struct Connection : RemoteStore::Connection
    {
        AutoCloseFD fd;

        int getFD() const override
        {
            return fd.get();
        }
    };

    kj::Promise<Result<void>> init(AutoCloseFD fd);
    std::optional<std::string> path;
};

class RpcRemoteStore : public UDSRemoteStore
{
    friend MustCallInit;

public:
    RpcRemoteStore(MustCallInit & w, Badge, UDSRemoteStoreConfig config, std::optional<std::string> path);

    /* Overrides for RPC-aware versions of RemoteStore commands */

    kj::Promise<Result<ref<const ValidPathInfo>>> addCAToStore(
        AsyncInputStream & dump,
        std::string_view name,
        ContentAddressMethod caMethod,
        HashType hashType,
        const StorePathSet & references,
        RepairFlag repair
    ) override;

    kj::Promise<Result<void>> addIndirectRoot(const Path & path) override;

    kj::Promise<Result<void>> addTempRoot(const StorePath & path) override;

    kj::Promise<Result<void>> ensurePath(const StorePath & path) override;

    kj::Promise<Result<bool>> isValidPathUncached(const StorePath & path, const Activity * context) override;

    kj::Promise<Result<void>> optimiseStore() override;

    kj::Promise<Result<std::map<std::string, StorePath>>>
    queryDerivationOutputMap(const StorePath & path) override;
    using RemoteStore::queryDerivationOutputMap;

    kj::Promise<Result<std::optional<StorePath>>>
    queryPathFromHashPart(const std::string & hashPart) override;

    kj::Promise<Result<StorePathSet>> querySubstitutablePaths(const StorePathSet & paths) override;

    kj::Promise<Result<void>> queryReferrers(const StorePath & path, StorePathSet & referrers) override;

    kj::Promise<Result<StorePathSet>> queryValidDerivers(const StorePath & path) override;

    kj::Promise<Result<StorePathSet>>
    queryValidPaths(const StorePathSet & paths, SubstituteFlag maybeSubstitute) override;

private:
    struct RpcState
    {
        kj::Own<kj::AsyncIoStream> rpcStream;
        box_ptr<AsyncFdIoStream> proxySock;
        box_ptr<capnp::TwoPartyClient> client;
        Activity loggerActivity;
        rpc::daemon::LegacyProtocol::Client legacyProtocol;
        rpc::daemon::LegacyStream::Client requestStream;

        kj::Promise<void> forwarder;
        std::exception_ptr error;

        kj::Promise<void> forwardRequests();
    };

    // this class is lifetime-bound to its rpc state; requestStream holds onto one of
    // these proxies. ideally we'd use RpcState itself for this, but kj does not have
    // shared pointers and cannot provide capabilities through anything except `Own`.
    struct LegacyStreamProxy final : rpc::daemon::LegacyStream::Server
    {
        RpcState & state;

        LegacyStreamProxy(RpcState & state) : state(state) {}

        kj::Promise<void> feed(FeedContext context) override;
        kj::Promise<void> sync(SyncContext context) override;
    };

    struct Connection : RemoteStore::Connection
    {
        AutoCloseFD fd;

        int getFD() const override
        {
            return fd.get();
        }
    };

    kj::Promise<Result<void>> init(AutoCloseFD fd);
    kj::Promise<Result<bool>> prepareRpcConnection(Connection & con);

    std::optional<std::string> path;
    std::shared_ptr<RpcState> rpc;
};

void registerUDSRemoteStore();

}
