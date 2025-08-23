#pragma once
///@file

#include "fs-accessor.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/gc-store.hh"
#include "lix/libstore/log-store.hh"
#include "lix/libutil/async-io.hh"

namespace nix {

struct LocalFSStoreConfig : virtual StoreConfig
{
    using StoreConfig::StoreConfig;

    const PathsSetting<std::optional<Path>>  rootDir{this, std::nullopt,
        "root",
        "Directory prefixed to all other paths."};

    const PathsSetting<Path> stateDir{this,
        rootDir.get() ? *rootDir.get() + "/nix/var/nix" : settings.nixStateDir,
        "state",
        "Directory where Lix will store state."};

    const PathsSetting<Path> logDir{this,
        rootDir.get() ? *rootDir.get() + "/nix/var/log/nix" : settings.nixLogDir,
        "log",
        "directory where Lix will store log files."};

    const PathsSetting<Path> realStoreDir{this,
        rootDir.get() ? *rootDir.get() + "/nix/store" : storeDir, "real",
        "Physical path of the Nix store."};
};

class LocalFSStore : public virtual Store,
    public virtual GcStore,
    public virtual LogStore
{
public:
    inline static std::string operationName = "Local Filesystem Store";

    const static std::string drvsLogDir;

    LocalFSStoreConfig & config() override = 0;
    const LocalFSStoreConfig & config() const override = 0;

    kj::Promise<Result<box_ptr<AsyncInputStream>>>
    narFromPath(const StorePath & path, const Activity * context) override;
    ref<FSAccessor> getFSAccessor() override;

    /**
     * Creates symlink from the `gcRoot` to the `storePath` and
     * registers the `gcRoot` as a permanent GC root. The `gcRoot`
     * symlink lives outside the store and is created and owned by the
     * user.
     *
     * @param gcRoot The location of the symlink.
     *
     * @param storePath The store object being rooted. The symlink will
     * point to `toRealPath(store.printStorePath(storePath))`.
     *
     * How the permanent GC root corresponding to this symlink is
     * managed is implementation-specific.
     */
    virtual kj::Promise<Result<Path>>
    addPermRoot(const StorePath & storePath, const Path & gcRoot) = 0;

    virtual Path getRealStoreDir() { return config().realStoreDir; }

    Path toRealPath(const Path & storePath) override
    {
        assert(isInStore(storePath));
        return getRealStoreDir() + "/" + std::string(storePath, config().storeDir.size() + 1);
    }

    kj::Promise<Result<std::optional<std::string>>> getBuildLogExact(const StorePath & path) override;

};

struct LocalStoreAccessor : public FSAccessor
{
    ref<LocalFSStore> store;

    LocalStoreAccessor(ref<LocalFSStore> store) : store(store) {}

    virtual kj::Promise<Result<Path>> toRealPath(const Path & path, bool requireValidPath = true);

    kj::Promise<Result<FSAccessor::Stat>> stat(const Path & path) override;

    kj::Promise<Result<StringSet>> readDirectory(const Path & path) override;

    kj::Promise<Result<std::string>>
    readFile(const Path & path, bool requireValidPath = true) override;

    kj::Promise<Result<std::string>> readLink(const Path & path) override;
};
}
