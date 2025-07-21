#pragma once
///@file

#include "lix/libstore/crypto.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/log-store.hh"

#include "lix/libutil/archive.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/pool.hh"

#include <atomic>

namespace nix {

struct NarInfo;

struct BinaryCacheStoreConfig : virtual StoreConfig
{
    using StoreConfig::StoreConfig;

    const Setting<std::string> compression{this, "xz", "compression",
        "NAR compression method (`xz`, `bzip2`, `gzip`, `zstd`, or `none`)."};

    const Setting<bool> writeNARListing{this, false, "write-nar-listing",
        "Whether to write a JSON file that lists the files in each NAR."};

    const Setting<bool> writeDebugInfo{this, false, "index-debug-info",
        R"(
          Whether to index DWARF debug info files by build ID. This allows [`dwarffs`](https://github.com/edolstra/dwarffs) to
          fetch debug info on demand
        )"};

    const Setting<Path> secretKeyFile{this, "", "secret-key",
        "Path to the secret key used to sign the binary cache."};

    const Setting<Path> localNarCache{this, "", "local-nar-cache",
        "Path to a local cache of NARs fetched from this binary cache, used by commands such as `nix store cat`."};

    const Setting<bool> parallelCompression{this, false, "parallel-compression",
        "Enable multi-threaded compression of NARs. This is currently only available for `xz` and `zstd`."};

    const Setting<int> compressionLevel{this, -1, "compression-level",
        R"(
          The *preset level* to be used when compressing NARs.
          The meaning and accepted values depend on the compression method selected.
          `-1` specifies that the default compression level should be used.
        )"};
};


/**
 * @note subclasses must implement at least one of the two
 * virtual getFile() methods.
 */
class BinaryCacheStore : public virtual Store,
    public virtual LogStore
{

private:

    std::unique_ptr<SecretKey> secretKey;

protected:

    // The prefix under which realisation infos will be stored
    const std::string realisationsPrefix = "realisations";

    BinaryCacheStore(const BinaryCacheStoreConfig & config);

public:

    BinaryCacheStoreConfig & config() override = 0;
    const BinaryCacheStoreConfig & config() const override = 0;

    virtual kj::Promise<Result<bool>>
    fileExists(const std::string & path, const Activity * context = nullptr) = 0;

    virtual kj::Promise<Result<void>> upsertFile(
        const std::string & path,
        std::shared_ptr<std::basic_iostream<char>> istream,
        const std::string & mimeType,
        const Activity * context
    ) = 0;

    kj::Promise<Result<void>> upsertFile(
        const std::string & path,
        // FIXME: use std::string_view
        std::string && data,
        const std::string & mimeType,
        const Activity * context = nullptr
    );

    /**
     * Dump the contents of the specified file to a sink.
     */
    virtual kj::Promise<Result<box_ptr<AsyncInputStream>>>
    getFile(const std::string & path, const Activity * context = nullptr) = 0;

    virtual kj::Promise<Result<std::optional<std::string>>>
    getFileContents(const std::string & path, const Activity * context = nullptr);

public:

    virtual kj::Promise<Result<void>> init() override;

private:

    std::string narMagic;

    std::string narInfoFileFor(const StorePath & storePath);

    kj::Promise<Result<void>>
    writeNarInfo(ref<NarInfo> narInfo, const Activity * context = nullptr);

    kj::Promise<Result<ref<const ValidPathInfo>>> addToStoreCommon(
        AsyncInputStream & narSource,
        RepairFlag repair,
        CheckSigsFlag checkSigs,
        const Activity * context,
        std::function<ValidPathInfo(HashResult)> mkInfo
    );

public:

    kj::Promise<Result<bool>>
    isValidPathUncached(const StorePath & path, const Activity * context = nullptr) override;

    kj::Promise<Result<std::shared_ptr<const ValidPathInfo>>>
    queryPathInfoUncached(const StorePath & path, const Activity * context = nullptr) override;

    kj::Promise<Result<std::optional<StorePath>>>
    queryPathFromHashPart(const std::string & hashPart) override;

    kj::Promise<Result<void>> addToStore(
        const ValidPathInfo & info,
        AsyncInputStream & narSource,
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

    kj::Promise<Result<StorePath>> addToStoreRecursive(
        std::string_view name,
        const PreparedDump & source,
        HashType hashAlgo,
        RepairFlag repair) override;
    kj::Promise<Result<StorePath>> addToStoreFlat(
        std::string_view name,
        const Path & srcPath,
        HashType hashAlgo,
        RepairFlag repair) override;

    kj::Promise<Result<StorePath>> addTextToStore(
        std::string_view name,
        std::string_view s,
        const StorePathSet & references,
        RepairFlag repair) override;

    kj::Promise<Result<box_ptr<AsyncInputStream>>>
    narFromPath(const StorePath & path, const Activity * context) override;

    ref<FSAccessor> getFSAccessor() override;

    kj::Promise<Result<void>>
    addSignatures(const StorePath & storePath, const StringSet & sigs) override;

    kj::Promise<Result<std::optional<std::string>>> getBuildLogExact(const StorePath & path) override;

    kj::Promise<Result<void>> addBuildLog(const StorePath & drvPath, std::string_view log) override;

};

MakeError(NoSuchBinaryCacheFile, Error);

}
