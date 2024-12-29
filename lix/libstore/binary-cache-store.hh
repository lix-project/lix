#pragma once
///@file

#include "lix/libstore/crypto.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/log-store.hh"

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

    virtual bool fileExists(const std::string & path) = 0;

    virtual void upsertFile(const std::string & path,
        std::shared_ptr<std::basic_iostream<char>> istream,
        const std::string & mimeType) = 0;

    void upsertFile(const std::string & path,
        // FIXME: use std::string_view
        std::string && data,
        const std::string & mimeType);

    /**
     * Dump the contents of the specified file to a sink.
     */
    virtual box_ptr<Source> getFile(const std::string & path);

    virtual std::optional<std::string> getFileContents(const std::string & path);

public:

    virtual void init() override;

private:

    std::string narMagic;

    std::string narInfoFileFor(const StorePath & storePath);

    void writeNarInfo(ref<NarInfo> narInfo);

    ref<const ValidPathInfo> addToStoreCommon(
        Source & narSource, RepairFlag repair, CheckSigsFlag checkSigs,
        std::function<ValidPathInfo(HashResult)> mkInfo);

public:

    bool isValidPathUncached(const StorePath & path) override;

    std::shared_ptr<const ValidPathInfo> queryPathInfoUncached(const StorePath & path) override;

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override;

    void addToStore(const ValidPathInfo & info, Source & narSource,
        RepairFlag repair, CheckSigsFlag checkSigs) override;

    StorePath addToStoreFromDump(Source & dump, std::string_view name,
        FileIngestionMethod method, HashType hashAlgo, RepairFlag repair, const StorePathSet & references) override;

    StorePath addToStore(
        std::string_view name,
        const Path & srcPath,
        FileIngestionMethod method,
        HashType hashAlgo,
        PathFilter & filter,
        RepairFlag repair,
        const StorePathSet & references) override;

    StorePath addTextToStore(
        std::string_view name,
        std::string_view s,
        const StorePathSet & references,
        RepairFlag repair) override;

    void registerDrvOutput(const Realisation & info) override;

    std::shared_ptr<const Realisation> queryRealisationUncached(const DrvOutput &) override;

    WireFormatGenerator narFromPath(const StorePath & path) override;

    ref<FSAccessor> getFSAccessor() override;

    void addSignatures(const StorePath & storePath, const StringSet & sigs) override;

    std::optional<std::string> getBuildLogExact(const StorePath & path) override;

    void addBuildLog(const StorePath & drvPath, std::string_view log) override;

};

MakeError(NoSuchBinaryCacheFile, Error);

}