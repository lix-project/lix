#pragma once
///@file

#include "lix/libstore/remote-store.hh"
#include "lix/libstore/remote-store-connection.hh"
#include "lix/libstore/indirect-root-store.hh"
#include "lix/libutil/async-io.hh"

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
    UDSRemoteStoreConfig config_;

public:

    UDSRemoteStore(UDSRemoteStoreConfig config);
    UDSRemoteStore(const std::string scheme, std::string path, UDSRemoteStoreConfig config);

    UDSRemoteStoreConfig & config() override { return config_; }
    const UDSRemoteStoreConfig & config() const override { return config_; }

    std::string getUri() override;

    static std::set<std::string> uriSchemes()
    { return {"unix"}; }

    ref<FSAccessor> getFSAccessor() override
    { return LocalFSStore::getFSAccessor(); }

    kj::Promise<Result<box_ptr<AsyncInputStream>>>
    narFromPath(const StorePath & path, const Activity * context) override
    {
        return LocalFSStore::narFromPath(path, context);
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

    kj::Promise<Result<ref<RemoteStore::Connection>>> openConnection() override;
    std::optional<std::string> path;
};

void registerUDSRemoteStore();

}
