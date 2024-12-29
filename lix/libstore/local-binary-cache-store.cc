#include "lix/libstore/local-binary-cache-store.hh"
#include "lix/libstore/binary-cache-store.hh"
#include "lix/libstore/globals.hh"
#include "lix/libstore/nar-info-disk-cache.hh"

#include <atomic>

namespace nix {

struct LocalBinaryCacheStoreConfig final : BinaryCacheStoreConfig
{
    using BinaryCacheStoreConfig::BinaryCacheStoreConfig;

    const std::string name() override { return "Local Binary Cache Store"; }

    std::string doc() override
    {
        return
          #include "local-binary-cache-store.md"
          ;
    }
};

class LocalBinaryCacheStore final : public BinaryCacheStore
{
private:

    LocalBinaryCacheStoreConfig config_;
    Path binaryCacheDir;

public:

    LocalBinaryCacheStoreConfig & config() override { return config_; }
    const LocalBinaryCacheStoreConfig & config() const override { return config_; }

    LocalBinaryCacheStore(
        const std::string scheme,
        const Path & binaryCacheDir,
        LocalBinaryCacheStoreConfig config)
        : Store(config)
        , BinaryCacheStore(config)
        , config_(std::move(config))
        , binaryCacheDir(binaryCacheDir)
    {
    }

    void init() override;

    std::string getUri() override
    {
        return "file://" + binaryCacheDir;
    }

    static std::set<std::string> uriSchemes();

protected:

    bool fileExists(const std::string & path) override;

    void upsertFile(const std::string & path,
        std::shared_ptr<std::basic_iostream<char>> istream,
        const std::string & mimeType) override
    {
        auto path2 = binaryCacheDir + "/" + path;
        static std::atomic<int> counter{0};
        Path tmp = fmt("%s.tmp.%d.%d", path2, getpid(), ++counter);
        AutoDelete del(tmp, false);
        StreamToSourceAdapter source(istream);
        writeFile(tmp, source);
        renameFile(tmp, path2);
        del.cancel();
    }

    box_ptr<Source> getFile(const std::string & path) override
    {
        try {
            return make_box_ptr<GeneratorSource>(readFileSource(binaryCacheDir + "/" + path));
        } catch (SysError & e) {
            if (e.errNo == ENOENT)
                throw NoSuchBinaryCacheFile("file '%s' does not exist in binary cache", path);
            throw;
        }
    }

    StorePathSet queryAllValidPaths() override
    {
        StorePathSet paths;

        for (auto & entry : readDirectory(binaryCacheDir)) {
            if (entry.name.size() != 40 ||
                !entry.name.ends_with(".narinfo"))
                continue;
            paths.insert(parseStorePath(
                    config_.storeDir + "/" + entry.name.substr(0, entry.name.size() - 8)
                    + "-" + MissingName));
        }

        return paths;
    }

    std::optional<TrustedFlag> isTrustedClient() override
    {
        return Trusted;
    }
};

void LocalBinaryCacheStore::init()
{
    createDirs(binaryCacheDir + "/nar");
    createDirs(binaryCacheDir + "/" + realisationsPrefix);
    if (config_.writeDebugInfo)
        createDirs(binaryCacheDir + "/debuginfo");
    createDirs(binaryCacheDir + "/log");
    BinaryCacheStore::init();
}

bool LocalBinaryCacheStore::fileExists(const std::string & path)
{
    return pathExists(binaryCacheDir + "/" + path);
}

std::set<std::string> LocalBinaryCacheStore::uriSchemes()
{
    if (getEnv("_NIX_FORCE_HTTP") == "1")
        return {};
    else
        return {"file"};
}

void registerLocalBinaryCacheStore() {
    StoreImplementations::add<LocalBinaryCacheStore, LocalBinaryCacheStoreConfig>();
}

}