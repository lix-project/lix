#pragma once
///@file

#include "lix/libutil/archive.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/json-fwd.hh"
#include "lix/libutil/logging.hh"
#include "lix/libstore/nar-info.hh"
#include "lix/libstore/realisation.hh"
#include "lix/libstore/path.hh"
#include "lix/libstore/derived-path.hh"
#include "lix/libutil/hash.hh"
#include "lix/libstore/content-address.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/serialise.hh"
#include "lix/libutil/lru-cache.hh"
#include "lix/libutil/sync.hh"
#include "lix/libstore/globals.hh"
#include "lix/libutil/config.hh"
#include "lix/libutil/json-fwd.hh"
#include "lix/libstore/path-info.hh"
#include "lix/libutil/repair-flag.hh"
#include "lix/libutil/source-path.hh"

#include <kj/async.h>
#include <atomic>
#include <limits>
#include <map>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <string>
#include <chrono>
#include <variant>


namespace nix {

/**
 * About the class hierarchy of the store implementations:
 *
 * Each store type `Foo` consists of two classes:
 *
 * 1. A class `FooConfig : virtual StoreConfig` that contains the configuration
 *   for the store
 *
 *   It should only contain members of type `const Setting<T>` (or subclasses
 *   of it) and inherit the constructors of `StoreConfig`
 *   (`using StoreConfig::StoreConfig`).
 *
 * 2. A class `Foo : virtual Store` that contains the
 *   implementation of the store.
 *
 *   This class is expected to have a constructor `Foo(FooConfig config)`
 *   that calls `StoreConfig(config)` (otherwise you're gonna encounter an
 *   `assertion failure` when trying to instantiate it).
 *
 * You can then register the new store by defining a registration function
 * (using `StoreImplementations::add`) and calling it in
 * `registerStoreImplementations` in `globals.cc`.
 */

MakeError(SubstError, Error);
/**
 * denotes a permanent build failure
 */
MakeError(BuildError, Error);
/**
 * denotes that a path in the store did not exist (but it could, had it
 * been put there, i.e. it is still legal).
 */
MakeError(InvalidPath, Error);
MakeError(Unsupported, Error);
MakeError(SubstituteGone, Error);
MakeError(SubstituterDisabled, Error);
/**
 * denotes that a path could not possibly be a store path.
 * e.g. outside of the nix store, illegal characters in the name, etc.
*/
MakeError(BadStorePath, Error);

MakeError(InvalidStoreURI, Error);

struct BasicDerivation;
struct Derivation;
class FSAccessor;
class NarInfoDiskCache;
class Store;


typedef std::map<std::string, StorePath> OutputPathMap;


enum CheckSigsFlag : bool { NoCheckSigs = false, CheckSigs = true };
enum SubstituteFlag : bool { NoSubstitute = false, Substitute = true };
enum AllowInvalidFlag : bool { DisallowInvalid = false, AllowInvalid = true };

/**
 * Magic header of exportPath() output (obsolete).
 */
const uint32_t exportMagic = 0x4558494e;


enum BuildMode { bmNormal, bmRepair, bmCheck };
/** Checks that a build mode is a valid one, then returns it */
BuildMode buildModeFromInteger(int);

enum TrustedFlag : bool { NotTrusted = false, Trusted = true };

template<>
struct json::is_integral_enum<TrustedFlag> : std::true_type {};

struct BuildResult;
struct KeyedBuildResult;


typedef std::map<StorePath, std::optional<ContentAddress>> StorePathCAMap;

struct StoreConfig : public Config
{
    typedef std::map<std::string, std::string> Params;

    using Config::Config;

    StoreConfig() = delete;

    static StringSet getDefaultSystemFeatures();

    virtual ~StoreConfig() noexcept(false) { }

    /**
     * The name of this type of store.
     */
    virtual const std::string name() = 0;

    /**
     * Documentation for this type of store.
     */
    virtual std::string doc()
    {
        return "";
    }

    /**
     * An experimental feature this type store is gated, if it is to be
     * experimental.
     */
    virtual std::optional<ExperimentalFeature> experimentalFeature() const
    {
        return std::nullopt;
    }

    const PathsSetting<Path> storeDir_{this, settings.nixStore,
        "store",
        R"(
          Logical location of the Nix store, usually
          `/nix/store`. Note that you can only copy store paths
          between stores if they have the same `store` setting.
        )"};
    const Path storeDir = storeDir_;

    const Setting<int> pathInfoCacheSize{this, 65536, "path-info-cache-size",
        "Size of the in-memory store path metadata cache."};

    const Setting<bool> isTrusted{this, false, "trusted",
        R"(
          Whether paths from this store can be used as substitutes
          even if they are not signed by a key listed in the
          [`trusted-public-keys`](@docroot@/command-ref/conf-file.md#conf-trusted-public-keys)
          setting.
        )"};

    Setting<int> priority{this, 0, "priority",
        R"(
          Priority of this store when used as a substituter. A lower value means a higher priority.
        )"};

    Setting<bool> wantMassQuery{this, false, "want-mass-query",
        R"(
          Whether this store (when used as a substituter) can be
          queried efficiently for path validity.
        )"};

    Setting<StringSet> systemFeatures{this, getDefaultSystemFeatures(),
        "system-features",
        "Optional features that the system this store builds on implements (like \"kvm\").",
        {},
        // Don't document the machine-specific default value
        false};
};

class Store : public std::enable_shared_from_this<Store>
{
protected:

    struct PathInfoCacheValue {

        /**
         * Time of cache entry creation or update
         */
        std::chrono::time_point<std::chrono::steady_clock> time_point = std::chrono::steady_clock::now();

        /**
         * Null if missing
         */
        std::shared_ptr<const ValidPathInfo> value;

        /**
         * Whether the value is valid as a cache entry. The path may not
         * exist.
         */
        bool isKnownNow();

        /**
         * Past tense, because a path can only be assumed to exists when
         * isKnownNow() && didExist()
         */
        inline bool didExist() {
          return value != nullptr;
        }
    };

    struct State
    {
        LRUCache<std::string, PathInfoCacheValue> pathInfoCache;
    };

    Sync<State, AsyncMutex> state;

    std::shared_ptr<NarInfoDiskCache> diskCache;

    Store(const StoreConfig & config);

public:
    struct AssociatedCredentials
    {
        uid_t user;
        gid_t group;
    };

    /**
     * Credentials of the context using this store if this store is proxied
     * to somewhere else and the peer context is known. Only the daemon can
     * set this to values that make any sense, using unix peer credentials.
     */
    virtual std::optional<AssociatedCredentials> associatedCredentials() const
    {
        return {};
    }

    /**
     * Perform any necessary effectful operation to make the store up and
     * running
     */
    virtual kj::Promise<Result<void>> init()
    {
        return {result::success()};
    }

    virtual StoreConfig & config() = 0;
    virtual const StoreConfig & config() const = 0;

    virtual ~Store() noexcept(false) { }

    virtual std::string getUri() = 0;

    /**
     * whether this store can safely be used by multiple threads. Stores with
     * async state (such as network connections) cannot be thread-safe due to
     * kj async objects' thread binding. realistically only stores using only
     * file system state can be thread-safe, i.e. only our local stores. even
     * daemon stores can't be safe as they hold onto unix socket connections.
     */
    virtual bool isThreadSafe() const
    {
        return false;
    }

    StorePath parseStorePath(std::string_view path) const;

    std::optional<StorePath> maybeParseStorePath(std::string_view path) const;

    std::string printStorePath(const StorePath & path) const;

    /**
     * Deprecated
     *
     * \todo remove
     */
    StorePathSet parseStorePathSet(const PathSet & paths) const;

    PathSet printStorePathSet(const StorePathSet & path) const;

    /**
     * Display a set of paths in human-readable form (i.e., between quotes
     * and separated by commas).
     */
    std::string showPaths(const StorePathSet & paths);

    /**
     * @return true if ‘path’ is in the Nix store (but not the Nix
     * store itself).
     */
    bool isInStore(PathView path) const;

    /**
     * @return true if ‘path’ is a store path, i.e. a direct child of the
     * Nix store.
     */
    bool isStorePath(std::string_view path) const;

    /**
     * Split a path like /nix/store/<hash>-<name>/<bla> into
     * /nix/store/<hash>-<name> and /<bla>.
     */
    std::pair<StorePath, Path> toStorePath(PathView path) const;

    /**
     * Follow symlinks until we end up with a path in the Nix store.
     */
    Path followLinksToStore(std::string_view path) const;

    /**
     * Same as followLinksToStore(), but apply toStorePath() to the
     * result.
     */
    StorePath followLinksToStorePath(std::string_view path) const;

    /**
     * Constructs a unique store path name.
     */
    StorePath makeStorePath(std::string_view type,
        std::string_view hash, std::string_view name) const;
    StorePath makeStorePath(std::string_view type,
        const Hash & hash, std::string_view name) const;

    StorePath makeOutputPath(std::string_view id,
        const Hash & hash, std::string_view name) const;

    StorePath makeFixedOutputPath(std::string_view name, const FixedOutputInfo & info) const;

    StorePath makeTextPath(std::string_view name, const TextInfo & info) const;

    StorePath makeFixedOutputPathFromCA(std::string_view name, const ContentAddressWithReferences & ca) const;

    /**
     * Preparatory part of addToStore().
     *
     * @return the store path to which srcPath is to be copied.
     */
    StorePath
    computeStorePathForPathRecursive(std::string_view name, const PreparedDump & source) const;
    StorePath computeStorePathForPathFlat(std::string_view name, const Path & srcPath) const;

    /**
     * Preparatory part of addTextToStore().
     *
     * !!! Computation of the path should take the references given to
     * addTextToStore() into account, otherwise we have a (relatively
     * minor) security hole: a caller can register a source file with
     * bogus references.  If there are too many references, the path may
     * not be garbage collected when it has to be (not really a problem,
     * the caller could create a root anyway), or it may be garbage
     * collected when it shouldn't be (more serious).
     *
     * Hashing the references would solve this (bogus references would
     * simply yield a different store path, so other users wouldn't be
     * affected), but it has some backwards compatibility issues (the
     * hashing scheme changes), so I'm not doing that for now.
     */
    StorePath computeStorePathForText(
        std::string_view name,
        std::string_view s,
        const StorePathSet & references) const;

    /**
     * Check whether a path is valid.
     * A path is valid when it exists in the store *now*.
     */
    kj::Promise<Result<bool>>
    isValidPath(const StorePath & path, const Activity * context = nullptr);

protected:

    virtual kj::Promise<Result<bool>>
    isValidPathUncached(const StorePath & path, const Activity * context = nullptr);

public:

    /**
     * If requested, substitute missing paths. This
     * implements nix-copy-closure's --use-substitutes
     * flag.
     */
    kj::Promise<Result<void>> substitutePaths(const StorePathSet & paths);

    /**
     * Query which of the given paths is valid. Optionally, try to
     * substitute missing paths.
     */
    virtual kj::Promise<Result<StorePathSet>> queryValidPaths(const StorePathSet & paths,
        SubstituteFlag maybeSubstitute = NoSubstitute);

    /**
     * Query the set of all valid paths. Note that for some store
     * backends, the name part of store paths may be replaced by 'x'
     * (i.e. you'll get /nix/store/<hash>-x rather than
     * /nix/store/<hash>-<name>). Use queryPathInfo() to obtain the
     * full store path. FIXME: should return a set of
     * std::variant<StorePath, HashPart> to get rid of this hack.
     */
    virtual kj::Promise<Result<StorePathSet>> queryAllValidPaths()
    try { unsupported("queryAllValidPaths"); } catch (...) { return {result::current_exception()}; }

    constexpr static const char * MissingName = "x";

    /**
     * Query information about a valid path. It is permitted to omit
     * the name part of the store path.
     */
    kj::Promise<Result<ref<const ValidPathInfo>>>
    queryPathInfo(const StorePath & path, const Activity * context = nullptr);

    /**
     * Check whether the given valid path info is sufficiently attested, by
     * either being signed by a trusted public key or content-addressed, in
     * order to be included in the given store.
     *
     * These same checks would be performed in addToStore, but this allows an
     * earlier failure in the case where dependencies need to be added too, but
     * the addToStore wouldn't fail until those dependencies are added. Also,
     * we don't really want to add the dependencies listed in a nar info we
     * don't trust anyyways.
     */
    virtual bool pathInfoIsUntrusted(const ValidPathInfo &)
    {
        return true;
    }

protected:

    /**
     * Queries the path info without caching.
     * Note to implementors: should return `nullptr` when the path is not found.
     */
    virtual kj::Promise<Result<std::shared_ptr<const ValidPathInfo>>>
    queryPathInfoUncached(const StorePath & path, const Activity * context = nullptr) = 0;

public:

    /**
     * Queries the set of incoming FS references for a store path.
     * The result is not cleared.
     */
    virtual kj::Promise<Result<void>>
    queryReferrers(const StorePath & path, StorePathSet & referrers)
    try { unsupported("queryReferrers"); } catch (...) { return {result::current_exception()}; }

    /**
     * @return all currently valid derivations that have `path` as an
     * output.
     *
     * (Note that the result of `queryDeriver()` is the derivation that
     * was actually used to produce `path`, which may not exist
     * anymore.)
     */
    virtual kj::Promise<Result<StorePathSet>> queryValidDerivers(const StorePath & path)
    {
        return {StorePathSet{}};
    }

    /**
     * Query the outputs of the derivation denoted by `path`.
     */
    virtual kj::Promise<Result<StorePathSet>> queryDerivationOutputs(const StorePath & path);

    /**
     * Query the mapping outputName => outputPath for the given
     * derivation.
     */
    virtual kj::Promise<Result<std::map<std::string, StorePath>>>
    queryDerivationOutputMap(const StorePath & path, Store * evalStore = nullptr);

    /**
     * Like `queryPartialDerivationOutputMap` but only considers
     * statically known output paths (i.e. those that can be gotten from
     * the derivation itself.
     *
     * Just a helper function for implementing
     * `queryDerivationOutputMap`.
     */
    virtual kj::Promise<Result<std::map<std::string, StorePath>>>
    queryStaticDerivationOutputMap(const StorePath & path);

    /**
     * Query the full store path given the hash part of a valid store
     * path, or empty if the path doesn't exist.
     */
    virtual kj::Promise<Result<std::optional<StorePath>>>
    queryPathFromHashPart(const std::string & hashPart) = 0;

    /**
     * Query which of the given paths have substitutes.
     */
    virtual kj::Promise<Result<StorePathSet>> querySubstitutablePaths(const StorePathSet & paths)
    {
        return {StorePathSet{}};
    }

    /**
     * Query substitute info (i.e. references, derivers and download
     * sizes) of a map of paths to their optional ca values. The info of
     * the first succeeding substituter for each path will be returned.
     * If a path does not have substitute info, it's omitted from the
     * resulting ‘infos’ map.
     */
    virtual kj::Promise<Result<void>> querySubstitutablePathInfos(const StorePathCAMap & paths,
        SubstitutablePathInfos & infos);

    /**
     * Import a path into the store.
     */
    virtual kj::Promise<Result<void>> addToStore(
        const ValidPathInfo & info,
        AsyncInputStream & narSource,
        RepairFlag repair = NoRepair,
        CheckSigsFlag checkSigs = CheckSigs,
        const Activity * context = nullptr
    ) = 0;

    /**
     * A list of paths infos along with a source providing the content
     * of the associated store path
     */
    using PathsSource = std::vector<
        std::pair<ValidPathInfo, std::function<kj::Promise<Result<box_ptr<AsyncInputStream>>>()>>>;

    /**
     * Import multiple paths into the store.
     */
    virtual kj::Promise<Result<void>> addMultipleToStore(
        PathsSource & pathsToCopy,
        Activity & act,
        RepairFlag repair = NoRepair,
        CheckSigsFlag checkSigs = CheckSigs);

    /**
     * Copy the contents of a path to the store and register the
     * validity the resulting path.
     *
     * @return The resulting path is returned.
     * @param filter This function can be used to exclude files (see
     * libutil/archive.hh).
     */
    virtual kj::Promise<Result<StorePath>> addToStoreRecursive(
        std::string_view name,
        const PreparedDump & source,
        HashType hashAlgo = HashType::SHA256,
        RepairFlag repair = NoRepair);
    virtual kj::Promise<Result<StorePath>> addToStoreFlat(
        std::string_view name,
        const Path & srcPath,
        HashType hashAlgo = HashType::SHA256,
        RepairFlag repair = NoRepair);

    /**
     * Copy the contents of a path to the store and register the
     * validity the resulting path, using a constant amount of
     * memory.
     */
    kj::Promise<Result<ValidPathInfo>> addToStoreSlow(std::string_view name, const Path & srcPath,
        FileIngestionMethod method = FileIngestionMethod::Recursive, HashType hashAlgo = HashType::SHA256,
        std::optional<Hash> expectedCAHash = {});

    /**
     * Like addToStore(), but the contents of the path are contained
     * in `dump`, which is either a NAR serialisation (if recursive ==
     * true) or simply the contents of a regular file (if recursive ==
     * false).
     * `dump` may be drained
     *
     * \todo remove?
     */
    virtual kj::Promise<Result<StorePath>> addToStoreFromDump(
        AsyncInputStream & dump,
        std::string_view name,
        FileIngestionMethod method = FileIngestionMethod::Recursive,
        HashType hashAlgo = HashType::SHA256,
        RepairFlag repair = NoRepair,
        const StorePathSet & references = StorePathSet()
    )
    try { unsupported("addToStoreFromDump"); } catch (...) { return {result::current_exception()}; }

    /**
     * Like addToStore, but the contents written to the output path is a
     * regular file containing the given string.
     */
    virtual kj::Promise<Result<StorePath>> addTextToStore(
        std::string_view name,
        std::string_view s,
        const StorePathSet & references,
        RepairFlag repair = NoRepair) = 0;

    /**
     * Generate a NAR dump of a store path.
     */
    virtual kj::Promise<Result<box_ptr<AsyncInputStream>>>
    narFromPath(const StorePath & path, const Activity * context = nullptr) = 0;

    /**
     * For each path, if it's a derivation, build it.  Building a
     * derivation means ensuring that the output paths are valid.  If
     * they are already valid, this is a no-op.  Otherwise, validity
     * can be reached in two ways.  First, if the output paths is
     * substitutable, then build the path that way.  Second, the
     * output paths can be created by running the builder, after
     * recursively building any sub-derivations. For inputs that are
     * not derivations, substitute them.
     */
    virtual kj::Promise<Result<void>> buildPaths(
        const std::vector<DerivedPath> & paths,
        BuildMode buildMode = bmNormal,
        std::shared_ptr<Store> evalStore = nullptr);

    /**
     * Like buildPaths(), but return a vector of \ref BuildResult
     * BuildResults corresponding to each element in paths. Note that in
     * case of a build/substitution error, this function won't throw an
     * exception, but return a BuildResult containing an error message.
     */
    virtual kj::Promise<Result<std::vector<KeyedBuildResult>>> buildPathsWithResults(
        const std::vector<DerivedPath> & paths,
        BuildMode buildMode = bmNormal,
        std::shared_ptr<Store> evalStore = nullptr);

    /**
     * Build a single non-materialized derivation (i.e. not from an
     * on-disk .drv file).
     *
     * @param drvPath This is used to deduplicate worker goals so it is
     * imperative that is correct. That said, it doesn't literally need
     * to be store path that would be calculated from writing this
     * derivation to the store: it is OK if it instead is that of a
     * Derivation which would resolve to this (by taking the outputs of
     * it's input derivations and adding them as input sources) such
     * that the build time referenceable-paths are the same.
     *
     * In the input-addressed case, we usually *do* use an "original"
     * unresolved derivations's path, as that is what will be used in the
     * buildPaths case. Also, the input-addressed output paths are verified
     * only by that contents of that specific unresolved derivation, so it is
     * nice to keep that information around so if the original derivation is
     * ever obtained later, it can be verified whether the trusted user in fact
     * used the proper output path.
     *
     * In the content-addressed case, we want to always use the resolved
     * drv path calculated from the provided derivation. This serves two
     * purposes:
     *
     *   - It keeps the operation trustless, by ruling out a maliciously
     *     invalid drv path corresponding to a non-resolution-equivalent
     *     derivation.
     *
     *   - For the floating case in particular, it ensures that the derivation
     *     to output mapping respects the resolution equivalence relation, so
     *     one cannot choose different resolution-equivalent derivations to
     *     subvert dependency coherence (i.e. the property that one doesn't end
     *     up with multiple different versions of dependencies without
     *     explicitly choosing to allow it).
     */
    virtual kj::Promise<Result<BuildResult>> buildDerivation(
        const StorePath & drvPath,
        const BasicDerivation & drv,
        BuildMode buildMode = bmNormal
    );

    /**
     * Ensure that a path is valid.  If it is not currently valid, it
     * may be made valid by running a substitute (if defined for the
     * path).
     */
    virtual kj::Promise<Result<void>> ensurePath(const StorePath & path);

    /**
     * Add a store path as a temporary root of the garbage collector.
     * The root disappears as soon as we exit.
     */
    virtual kj::Promise<Result<void>> addTempRoot(const StorePath & path)
    try {
        debug("not creating temporary root, store doesn't support GC");
        return {result::success()};
    } catch (...) {
        return {result::current_exception()};
    }

    /**
     * @return a string representing information about the path that
     * can be loaded into the database using `nix-store --load-db` or
     * `nix-store --register-validity`.
     */
    kj::Promise<Result<std::string>> makeValidityRegistration(const StorePathSet & paths,
        bool showDerivers, bool showHash);

    /**
     * Write a JSON representation of store path metadata, such as the
     * hash and the references.
     *
     * @param includeImpureInfo If true, variable elements such as the
     * registration time are included.
     *
     * @param showClosureSize If true, the closure size of each path is
     * included.
     */
    kj::Promise<Result<JSON>> pathInfoToJSON(const StorePathSet & storePaths,
        bool includeImpureInfo, bool showClosureSize,
        Base hashBase = Base::Base32,
        AllowInvalidFlag allowInvalid = DisallowInvalid);

    /**
     * @return the size of the closure of the specified path, that is,
     * the sum of the size of the NAR serialisation of each path in the
     * closure.
     */
    kj::Promise<Result<std::pair<uint64_t, uint64_t>>> getClosureSize(const StorePath & storePath);

    /**
     * Optimise the disk space usage of the Nix store by hard-linking files
     * with the same contents.
     */
    virtual kj::Promise<Result<void>> optimiseStore() { return {result::success()}; }

    /**
     * Check the integrity of the Nix store.
     *
     * @return true if errors remain.
     */
    virtual kj::Promise<Result<bool>> verifyStore(bool checkContents, RepairFlag repair = NoRepair)
    {
        return {false};
    }

    /**
     * @return An object to access files in the Nix store.
     */
    virtual ref<FSAccessor> getFSAccessor() = 0;

    /**
     * Repair the contents of the given path by redownloading it using
     * a substituter (if available).
     */
    virtual kj::Promise<Result<void>> repairPath(const StorePath & path);

    /**
     * Add signatures to the specified store path. The signatures are
     * not verified.
     */
    virtual kj::Promise<Result<void>>
    addSignatures(const StorePath & storePath, const StringSet & sigs)
    try { unsupported("addSignatures"); } catch (...) { return {result::current_exception()}; }

    /* Utility functions. */

    /**
     * Read a derivation, after ensuring its existence through
     * ensurePath().
     */
    kj::Promise<Result<Derivation>> derivationFromPath(const StorePath & drvPath);

    /**
     * Read a derivation (which must already be valid).
     */
    kj::Promise<Result<Derivation>> readDerivation(const StorePath & drvPath);

    /**
     * Read a derivation from a potentially invalid path.
     */
    kj::Promise<Result<Derivation>> readInvalidDerivation(const StorePath & drvPath);

    /**
     * @param [out] out Place in here the set of all store paths in the
     * file system closure of `storePath`; that is, all paths than can
     * be directly or indirectly reached from it. `out` is not cleared.
     *
     * @param flipDirection If true, the set of paths that can reach
     * `storePath` is returned; that is, the closures under the
     * `referrers` relation instead of the `references` relation is
     * returned.
     */
    virtual kj::Promise<Result<void>> computeFSClosure(const StorePathSet & paths,
        StorePathSet & out, bool flipDirection = false,
        bool includeOutputs = false, bool includeDerivers = false);

    kj::Promise<Result<void>> computeFSClosure(const StorePath & path,
        StorePathSet & out, bool flipDirection = false,
        bool includeOutputs = false, bool includeDerivers = false);

    /**
     * Given a set of paths that are to be built, return the set of
     * derivations that will be built, and the set of output paths that
     * will be substituted.
     */
    virtual kj::Promise<Result<void>> queryMissing(const std::vector<DerivedPath> & targets,
        StorePathSet & willBuild, StorePathSet & willSubstitute, StorePathSet & unknown,
        uint64_t & downloadSize, uint64_t & narSize);

    /**
     * Sort a set of paths topologically under the references
     * relation.  If p refers to q, then p precedes q in this list.
     */
    kj::Promise<Result<StorePaths>> topoSortPaths(const StorePathSet & paths);

    /**
     * Export multiple paths in the format expected by ‘nix-store
     * --import’.
     */
    kj::Promise<Result<void>> exportPaths(const StorePathSet & paths, Sink & sink);

    kj::Promise<Result<void>> exportPath(const StorePath & path, Sink & sink);

    /**
     * Import a sequence of NAR dumps created by exportPaths() into the
     * Nix store. Optionally, the contents of the NARs are preloaded
     * into the specified FS accessor to speed up subsequent access.
     */
    kj::Promise<Result<StorePaths>>
    importPaths(Source & source, CheckSigsFlag checkSigs = CheckSigs);

    template<template<typename> typename Wrapper = std::type_identity_t>
    struct Stats
    {
        Wrapper<uint64_t> narInfoRead{0};
        Wrapper<uint64_t> narInfoReadAverted{0};
        Wrapper<uint64_t> narInfoMissing{0};
        Wrapper<uint64_t> narInfoWrite{0};
        Wrapper<uint64_t> pathInfoCacheSize{0};
        Wrapper<uint64_t> narRead{0};
        Wrapper<uint64_t> narReadBytes{0};
        Wrapper<uint64_t> narReadCompressedBytes{0};
        Wrapper<uint64_t> narWrite{0};
        Wrapper<uint64_t> narWriteAverted{0};
        Wrapper<uint64_t> narWriteBytes{0};
        Wrapper<uint64_t> narWriteCompressedBytes{0};
        Wrapper<uint64_t> narWriteCompressionTimeMs{0};
    };

    kj::Promise<Result<Stats<>>> getStats();

    /**
     * Computes the full closure of of a set of store-paths for e.g.
     * derivations that need this information for `exportReferencesGraph`.
     */
    kj::Promise<Result<StorePathSet>>
    exportReferences(const StorePathSet & storePaths, const StorePathSet & inputPaths);

    /**
     * Given a store path, return the realisation actually used in the realisation of this path:
     * - If the path is a content-addressed derivation, try to resolve it
     * - Otherwise, find one of its derivers
     */
    kj::Promise<Result<std::optional<StorePath>>> getBuildDerivationPath(const StorePath &);

    /**
     * Hack to allow long-running processes like hydra-queue-runner to
     * occasionally flush their path info cache.
     */
    kj::Promise<void> clearPathInfoCache()
    {
        (co_await state.lock())->pathInfoCache.clear();
    }

    /**
     * Establish a connection to the store, for store types that have
     * a notion of connection. Otherwise this is a no-op.
     */
    virtual kj::Promise<Result<void>> connect() { return {result::success()}; }

    /**
     * Get the protocol version of this store or it's connection.
     */
    virtual kj::Promise<Result<unsigned int>> getProtocol()
    {
        return {result::success(0)};
    };

    /**
     * @return/ whether store trusts *us*.
     *
     * `std::nullopt` means we do not know.
     *
     * @note This is the opposite of the StoreConfig::isTrusted
     * store setting. That is about whether *we* trust the store.
     */
    virtual kj::Promise<Result<std::optional<TrustedFlag>>> isTrustedClient() = 0;


    virtual Path toRealPath(const Path & storePath)
    {
        return storePath;
    }

    Path toRealPath(const StorePath & storePath)
    {
        return toRealPath(printStorePath(storePath));
    }

    /**
     * Synchronises the options of the client with those of the daemon
     * (a no-op when there’s no daemon)
     */
    virtual kj::Promise<Result<void>> setOptions() { return {result::success()}; }

    virtual kj::Promise<Result<std::optional<std::string>>> getVersion()
    {
        return {result::success(std::nullopt)};
    }

protected:

    Stats<std::atomic> stats;

    /**
     * Helper for methods that are not unsupported: this is used for
     * default definitions for virtual methods that are meant to be overriden.
     *
     * \todo Using this should be a last resort. It is better to make
     * the method "virtual pure" and/or move it to a subclass.
     */
    [[noreturn]] void unsupported(const std::string & op)
    {
        throw Unsupported("operation '%s' is not supported by store '%s'", op, getUri());
    }

};


/**
 * Copy a path from one store to another.
 */
kj::Promise<Result<void>> copyStorePath(
    Store & srcStore,
    Store & dstStore,
    const StorePath & storePath,
    RepairFlag repair = NoRepair,
    CheckSigsFlag checkSigs = CheckSigs,
    const Activity * context = nullptr
);

/**
 * Copy store paths from one store to another. The paths may be copied
 * in parallel. They are copied in a topologically sorted order (i.e. if
 * A is a reference of B, then A is copied before B), but the set of
 * store paths is not automatically closed; use copyClosure() for that.
 *
 * @return a map of what each path was copied to the dstStore as.
 */
kj::Promise<Result<std::map<StorePath, StorePath>>> copyPaths(
    Store & srcStore, Store & dstStore,
    const RealisedPath::Set &,
    RepairFlag repair = NoRepair,
    CheckSigsFlag checkSigs = CheckSigs,
    SubstituteFlag substitute = NoSubstitute);

kj::Promise<Result<std::map<StorePath, StorePath>>> copyPaths(
    Store & srcStore, Store & dstStore,
    const StorePathSet & paths,
    RepairFlag repair = NoRepair,
    CheckSigsFlag checkSigs = CheckSigs,
    SubstituteFlag substitute = NoSubstitute);

/**
 * Copy the closure of `paths` from `srcStore` to `dstStore`.
 */
kj::Promise<Result<void>> copyClosure(
    Store & srcStore, Store & dstStore,
    const RealisedPath::Set & paths,
    RepairFlag repair = NoRepair,
    CheckSigsFlag checkSigs = CheckSigs,
    SubstituteFlag substitute = NoSubstitute);

kj::Promise<Result<void>> copyClosure(
    Store & srcStore, Store & dstStore,
    const StorePathSet & paths,
    RepairFlag repair = NoRepair,
    CheckSigsFlag checkSigs = CheckSigs,
    SubstituteFlag substitute = NoSubstitute);

/**
 * Remove the temporary roots file for this process.  Any temporary
 * root becomes garbage after this point unless it has been registered
 * as a (permanent) root.
 */
void removeTempRoots();


/**
 * Resolve the derived path completely, failing if any derivation output
 * is unknown.
 */
kj::Promise<Result<OutputPathMap>>
resolveDerivedPath(Store &, const DerivedPath::Built &, Store * evalStore = nullptr);

/**
 * Whether to allow daemon connections in openStore().
 */
enum class AllowDaemon
{
    Disallow,
    Allow,
};

/**
 * @return a Store object to access the Nix store denoted by
 * ‘uri’ (slight misnomer...).
 *
 * @param uri Supported values are:
 *
 * - ‘local’: The Nix store in /nix/store and database in
 *   /nix/var/nix/db, accessed directly.
 *
 * - ‘daemon’: The Nix store accessed via a Unix domain socket
 *   connection to nix-daemon.
 *
 * - ‘unix://<path>’: The Nix store accessed via a Unix domain socket
 *   connection to nix-daemon, with the socket located at <path>.
 *
 * - ‘auto’ or ‘’: Try `daemon` if the daemon socket exists and
 *   `allowDaemon` is `AllowDaemon::Allow`, and `local` otherwise.
 *
 * - ‘file://<path>’: A binary cache stored in <path>.
 *
 * - ‘https://<path>’: A binary cache accessed via HTTP.
 *
 * - ‘s3://<path>’: A writable binary cache stored on Amazon's Simple
 *   Storage Service.
 *
 * - ‘ssh://[user@]<host>’: A remote Nix store accessed by running
 *   ‘nix-store --serve’ via SSH.
 *
 * You can pass parameters to the store implementation by appending
 * ‘?key=value&key=value&...’ to the URI.
 *
 * @param allowDaemon Whether to allow connections to the daemon. The
 * default should only be overridden with very good reason. When this is
 * `AllowDaemon::Disallow`, `""` and `"auto"` URIs will only attempt the
 * ‘local’ method, and `"daemon"` URIs will cause a hard error.
 */
kj::Promise<Result<ref<Store>>> openStore(const std::string & uri = settings.storeUri.get(),
    const StoreConfig::Params & extraParams = {},
    AllowDaemon allowDaemon = AllowDaemon::Allow);

/**
 * @return the default substituter stores, defined by the
 * ‘substituters’ option and various legacy options.
 */
kj::Promise<Result<std::list<ref<Store>>>> getDefaultSubstituters();

struct StoreFactory
{
    std::set<std::string> uriSchemes;
    std::function<
        std::optional<ref<Store>> (
            const std::string & scheme,
            const std::string & uri,
            const StoreConfig::Params & params
        )
    > create;
    std::function<std::shared_ptr<StoreConfig> ()> getConfig;
};

struct StoreImplementations
{
    static std::vector<StoreFactory> * registered;

    template<typename T, typename TConfig>
    static void add()
    {
        if (!registered) registered = new std::vector<StoreFactory>();
        StoreFactory factory{
            .uriSchemes = T::uriSchemes(),
            .create =
                ([](const std::string & scheme, const std::string & uri, const StoreConfig::Params & params)
                 -> std::optional<ref<Store>>
                 { return make_ref<T>(scheme, uri, params); }),
            .getConfig =
                ([]()
                 -> std::shared_ptr<StoreConfig>
                 { return std::make_shared<TConfig>(StringMap({})); })
        };
        registered->push_back(factory);
    }
};


/**
 * Display a set of paths in human-readable form (i.e., between quotes
 * and separated by commas).
 */
std::string showPaths(const PathSet & paths);


std::optional<ValidPathInfo> decodeValidPathInfo(
    const Store & store,
    std::istream & str,
    std::optional<HashResult> hashGiven = std::nullopt);

/**
 * Split URI into protocol+hierarchy part and its parameter set.
 */
std::pair<std::string, StoreConfig::Params> splitUriAndParams(const std::string & uri);

const ContentAddress * getDerivationCA(const BasicDerivation & drv);

}
