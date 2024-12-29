#pragma once
///@file

#include "lix/libstore/remote-store.hh"
#include "lix/libstore/remote-store-connection.hh"
#include "lix/libstore/indirect-root-store.hh"

namespace nix {

struct UDSRemoteStoreConfig : virtual LocalFSStoreConfig, virtual RemoteStoreConfig
{
    UDSRemoteStoreConfig(const Params & params)
        : StoreConfig(params)
        , LocalFSStoreConfig(params)
        , RemoteStoreConfig(params)
    {
    }

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

    WireFormatGenerator narFromPath(const StorePath & path) override
    { return LocalFSStore::narFromPath(path); }

    /**
     * Implementation of `IndirectRootStore::addIndirectRoot()` which
     * delegates to the remote store.
     *
     * The idea is that the client makes the direct symlink, so it is
     * owned managed by the client's user account, and the server makes
     * the indirect symlink.
     */
    void addIndirectRoot(const Path & path) override;

private:

    struct Connection : RemoteStore::Connection
    {
        AutoCloseFD fd;
        void closeWrite() override;
    };

    ref<RemoteStore::Connection> openConnection() override;
    std::optional<std::string> path;
};

void registerUDSRemoteStore();

}