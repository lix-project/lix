#pragma once
///@file

#include "lix/libstore/sqlite.hh"

#include "lix/libstore/store-api.hh"
#include "lix/libstore/indirect-root-store.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/sync.hh"
#include "lix/libutil/types.hh"

#include <chrono>
#include <future>
#include <kj/async.h>
#include <string>
#include <mutex>
#include <memory>
#include <unordered_set>


namespace nix {


/**
 * Nix store and database schema version.
 *
 * Version 1 (or 0) was Nix <=
 * 0.7.  Version 2 was Nix 0.8 and 0.9.  Version 3 is Nix 0.10.
 * Version 4 is Nix 0.11.  Version 5 is Nix 0.12-0.16.  Version 6 is
 * Nix 1.0.  Version 7 is Nix 1.3. Version 10 is 2.0.
 *
 * Lix started at 2.90, it cannot ever go past version 10 (Nix 2.18),
 * since doing so will break compatibility with future CppNix versions.
 */
const int nixSchemaVersion = 10;


struct OptimiseStats
{
    unsigned long filesLinked = 0;
    uint64_t bytesFreed = 0;
    uint64_t blocksFreed = 0;
};

struct LocalStoreConfig final : LocalFSStoreConfig
{
    using LocalFSStoreConfig::LocalFSStoreConfig;

    Setting<bool> requireSigs{this,
        settings.requireSigs,
        "require-sigs",
        "Whether store paths copied into this store should have a trusted signature."};

    Setting<bool> readOnly{this,
        false,
        "read-only",
        R"(
          Allow this store to be opened when its [database](@docroot@/glossary.md#gloss-nix-database) is on a read-only filesystem.

          Normally Lix will attempt to open the store database in read-write mode, even for querying (when write access is not needed), causing it to fail if the database is on a read-only filesystem.

          Enable read-only mode to disable locking and open the SQLite database with the [`immutable` parameter](https://www.sqlite.org/c3ref/open.html) set.

          > **Warning**
          > Do not use this unless the filesystem is read-only.
          >
          > Using it when the filesystem is writable can cause incorrect query results or corruption errors if the database is changed by another process.
          > While the filesystem the database resides on might appear to be read-only, consider whether another user or system might have write access to it.
        )"};

    const std::string name() override { return "Local Store"; }

    std::string doc() override;
};

class LocalStore : public virtual IndirectRootStore
    , public virtual GcStore
{
private:

    LocalStoreConfig config_;

    /**
     * Lock file used for upgrading.
     */
    AutoCloseFD globalLock;

    /**
     * Trusted public keys by this store. Initialized lazily by getPublicKeys().
     *
     * Note that this lazy initialization is load-bearing: on the daemon, the
     * store is initialized very early, before settings including
     * trusted-public-keys are received from the client.
     */
    std::unique_ptr<const PublicKeys> publicKeys = nullptr;
    std::once_flag publicKeysFlag;

    struct DBState
    {
        /**
         * The SQLite database object.
         */
        SQLite db;

        struct Stmts;
        std::unique_ptr<Stmts> stmts;
    };

    Sync<DBState, AsyncMutex> _dbState;

    struct GCState
    {
        /**
         * The last time we checked whether to do an auto-GC, or an
         * auto-GC finished.
         */
        std::chrono::time_point<std::chrono::steady_clock> lastGCCheck;

        /**
         * Whether auto-GC is running. If so, get gcFuture to wait for
         * the GC to finish.
         */
        bool gcRunning = false;
        std::future<void> gcFuture;
        std::list<kj::Own<kj::CrossThreadPromiseFulfiller<void>>> gcWaiters;

        /**
         * How much disk space was available after the previous
         * auto-GC. If the current available disk space is below
         * minFree but not much below availAfterGC, then there is no
         * point in starting a new GC.
         */
        uint64_t availAfterGC = std::numeric_limits<uint64_t>::max();
    };

    Sync<GCState> _gcState;

    std::optional<AssociatedCredentials> association;

public:

    const Path dbDir;
    const Path linksDir;
    /** Path kept around to reserve some filesystem space to be able to begin a garbage collection */
    const Path reservedSpacePath;
    const Path schemaPath;
    const Path tempRootsDir;
    const Path fnTempRoots;

    LocalStoreConfig & config() override { return config_; }
    const LocalStoreConfig & config() const override { return config_; }

    std::optional<AssociatedCredentials> associatedCredentials() const override
    {
        return association;
    }

    void associateWithCredentials(uid_t user, gid_t group)
    {
        association = {user, group};
    }

    bool isThreadSafe() const override
    {
        return true;
    }

private:

    const PublicKeys & getPublicKeys();

protected:

    /**
     * Initialise the local store, upgrading the schema if
     * necessary.
     * Protected so that users don't accidentally create a LocalStore
     * instead of a platform's subclass.
     */
    LocalStore(LocalStoreConfig config);
    LocalStore(std::string scheme, std::string path, LocalStoreConfig config);

public:

    /**
     * Hack for build-remote.cc.
     */
    PathSet locksHeld;

    virtual ~LocalStore();

    static std::set<std::string> uriSchemes()
    { return {}; }

    /**
     * Create a LocalStore, possibly a platform-specific subclass
     */
    static ref<LocalStore> makeLocalStore(const StoreConfig::Params & params);

    /**
     * Implementations of abstract store API methods.
     */

    std::string getUri() override;

    kj::Promise<Result<bool>>
    isValidPathUncached(const StorePath & path, const Activity * context) override;

    kj::Promise<Result<StorePathSet>> queryValidPaths(const StorePathSet & paths,
        SubstituteFlag maybeSubstitute = NoSubstitute) override;

    kj::Promise<Result<StorePathSet>> queryAllValidPaths() override;

    kj::Promise<Result<std::shared_ptr<const ValidPathInfo>>>
    queryPathInfoUncached(const StorePath & path, const Activity * context) override;

    kj::Promise<Result<void>>
    queryReferrers(const StorePath & path, StorePathSet & referrers) override;

    kj::Promise<Result<StorePathSet>> queryValidDerivers(const StorePath & path) override;

    kj::Promise<Result<std::map<std::string, StorePath>>>
    queryStaticDerivationOutputMap(const StorePath & path) override;

    kj::Promise<Result<std::optional<StorePath>>>
    queryPathFromHashPart(const std::string & hashPart) override;

    kj::Promise<Result<StorePathSet>> querySubstitutablePaths(const StorePathSet & paths) override;

    bool pathInfoIsUntrusted(const ValidPathInfo &) override;

    kj::Promise<Result<void>> addToStore(
        const ValidPathInfo & info,
        AsyncInputStream & source,
        RepairFlag repair,
        CheckSigsFlag checkSigs,
        const Activity * context
    ) override;

    kj::Promise<Result<StorePath>> addToStoreFromDump(
        AsyncInputStream & dump,
        std::string_view name,
        FileIngestionMethod method,
        HashType hashAlgo,
        RepairFlag repair,
        const StorePathSet & references
    ) override;

    kj::Promise<Result<StorePath>> addTextToStore(
        std::string_view name,
        std::string_view s,
        const StorePathSet & references,
        RepairFlag repair) override;

    kj::Promise<Result<void>> addTempRoot(const StorePath & path) override;

private:

    void createTempRootsFile();

    /**
     * The file to which we write our temporary roots.
     */
    Sync<AutoCloseFD> _fdTempRoots;

    /**
     * The global GC lock.
     */
    Sync<AutoCloseFD> _fdGCLock;

    /**
     * Connection to the garbage collector.
     */
    Sync<AutoCloseFD> _fdRootsSocket;

public:

    /**
     * Implementation of IndirectRootStore::addIndirectRoot().
     *
     * The weak reference merely is a symlink to `path' from
     * /nix/var/nix/gcroots/auto/<hash of `path'>.
     */
    kj::Promise<Result<void>> addIndirectRoot(const Path & path) override;

private:

    void findTempRoots(Roots & roots, bool censor);

    AutoCloseFD openGCLock();

public:

    kj::Promise<Result<Roots>> findRoots(bool censor) override;

    kj::Promise<Result<void>>
    collectGarbage(const GCOptions & options, GCResults & results) override;

    /**
     * Optimise the disk space usage of the Nix store by hard-linking
     * files with the same contents.
     */
    kj::Promise<Result<void>> optimiseStore(OptimiseStats & stats);

    kj::Promise<Result<void>> optimiseStore() override;

    /**
     * Optimise a single store path. Optionally, test the encountered
     * symlinks for corruption.
     */
    void optimisePath(const Path & path, RepairFlag repair);

    kj::Promise<Result<bool>> verifyStore(bool checkContents, RepairFlag repair) override;

    /**
     * Register the validity of a path, i.e., that `path` exists, that
     * the paths referenced by it exists, and in the case of an output
     * path of a derivation, that it has been produced by a successful
     * execution of the derivation (or something equivalent).  Also
     * register the hash of the file system contents of the path.  The
     * hash must be a SHA-256 hash.
     */
    kj::Promise<Result<void>> registerValidPath(const ValidPathInfo & info);

    kj::Promise<Result<void>> registerValidPaths(const ValidPathInfos & infos);

    kj::Promise<Result<unsigned int>> getProtocol() override;

    kj::Promise<Result<std::optional<TrustedFlag>>> isTrustedClient() override;

    kj::Promise<Result<void>>
    addSignatures(const StorePath & storePath, const StringSet & sigs) override;

    /**
     * If free disk space in /nix/store if below minFree, delete
     * garbage until it exceeds maxFree.
     */
    kj::Promise<Result<void>> autoGC(bool sync = true);

    /**
     * Register the store path 'output' as the output named 'outputName' of
     * derivation 'deriver'.
     */
    void cacheDrvOutputMapping(
        DBState & state,
        const uint64_t deriver,
        const std::string & outputName,
        const StorePath & output);

    kj::Promise<Result<std::optional<std::string>>> getVersion() override;

private:

    /**
     * Retrieve the current version of the database schema.
     * If the database does not exist yet, the version returned will be 0.
     */
    int getSchema();

    void initDB(DBState & state);
    void openDB(DBState & state, bool create);
    void prepareStatements(DBState & state);

    void makeStoreWritable();

    uint64_t queryValidPathId(DBState & state, const StorePath & path);

    kj::Promise<Result<uint64_t>>
    addValidPath(DBState & state, const ValidPathInfo & info, bool checkOutputs = true);

    kj::Promise<Result<void>> invalidatePath(DBState & state, const StorePath & path);

    /**
     * Delete a path from the Nix store.
     */
    kj::Promise<Result<void>> invalidatePathChecked(const StorePath & path);

    kj::Promise<Result<void>> verifyPath(const StorePath & path, const StorePathSet & store,
        StorePathSet & done, StorePathSet & validPaths, RepairFlag repair, bool & errors);

    std::shared_ptr<const ValidPathInfo> queryPathInfoInternal(DBState & state, const StorePath & path);

    void updatePathInfo(DBState & state, const ValidPathInfo & info);

    PathSet queryValidPathsOld();
    ValidPathInfo queryPathInfoOld(const Path & path);

    kj::Promise<Result<void>> findRoots(const Path & path, unsigned char type, Roots & roots);

    kj::Promise<Result<void>> findRootsNoTemp(Roots & roots, bool censor);

    /**
     * Find possible garbage collector roots in a platform-specific manner,
     * e.g. by looking in `/proc` or using `lsof`
     */
    virtual kj::Promise<Result<void>> findPlatformRoots(UncheckedRoots & unchecked);

    kj::Promise<Result<void>> findRuntimeRoots(Roots & roots, bool censor);

    std::pair<Path, AutoCloseFD> createTempDirInStore();

    typedef std::unordered_set<ino_t> InodeHash;

    InodeHash loadInodeHash();
    Strings readDirectoryIgnoringInodes(const Path & path, const InodeHash & inodeHash);
    void optimisePath_(Activity * act, OptimiseStats & stats, const Path & path, InodeHash & inodeHash, RepairFlag repair);

    // Internal versions that are not wrapped in retry_sqlite.
    bool isValidPath_(DBState & state, const StorePath & path);
    void queryReferrers(DBState & state, const StorePath & path, StorePathSet & referrers);

    /**
     * Add signatures to a ValidPathInfo or Realisation using the secret keys
     * specified by the ‘secret-key-files’ option.
     */
    void signPathInfo(ValidPathInfo & info);

    // XXX: Make a generic `Store` method
    ContentAddress hashCAPath(
        const ContentAddressMethod & method,
        const HashType & hashType,
        const StorePath & path);

    ContentAddress hashCAPath(
        const ContentAddressMethod & method,
        const HashType & hashType,
        const Path & path,
        const std::string_view pathHash
    );

    kj::Promise<Result<void>> addBuildLog(const StorePath & drvPath, std::string_view log) override;

    friend struct LocalDerivationGoal;
    friend struct PathSubstitutionGoal;
    friend struct SubstitutionGoal;
    friend struct DerivationGoal;
};


typedef std::pair<dev_t, ino_t> Inode;
typedef std::set<Inode> InodesSeen;


/**
 * "Fix", or canonicalise, the meta-data of the files in a store path
 * after it has been built.  In particular:
 *
 * - the last modification date on each file is set to 1 (i.e.,
 *   00:00:01 1/1/1970 UTC)
 *
 * - the permissions are set of 444 or 555 (i.e., read-only with or
 *   without execute permission; setuid bits etc. are cleared)
 *
 * - the owner and group are set to the Nix user and group, if we're
 *   running as root.
 *
 * If uidRange is not empty, this function will throw an error if it
 * encounters files owned by a user outside of the closed interval
 * [uidRange->first, uidRange->second].
 */
void canonicalisePathMetaData(
    const Path & path,
    std::optional<std::pair<uid_t, uid_t>> uidRange,
    InodesSeen & inodesSeen);
void canonicalisePathMetaData(
    const Path & path,
    std::optional<std::pair<uid_t, uid_t>> uidRange);

void canonicaliseTimestampAndPermissions(const Path & path);

MakeError(PathInUse, Error);

// Implemented by the relevant platform/ module being used.
void registerLocalStore();

}
