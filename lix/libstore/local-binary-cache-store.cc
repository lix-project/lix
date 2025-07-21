#include "lix/libstore/local-binary-cache-store.hh"
#include "lix/libstore/binary-cache-store.hh"
#include "lix/libstore/globals.hh"
#include "lix/libstore/nar-info-disk-cache.hh"
#include "lix/libutil/async-io.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/result.hh"

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

    kj::Promise<Result<void>> init() override;

    std::string getUri() override
    {
        return "file://" + binaryCacheDir;
    }

    static std::set<std::string> uriSchemes();

protected:

    kj::Promise<Result<bool>>
    fileExists(const std::string & path, const Activity * context) override;

    kj::Promise<Result<void>> upsertFile(
        const std::string & path,
        std::shared_ptr<std::basic_iostream<char>> istream,
        const std::string & mimeType,
        const Activity * context
    ) override
    try {
        auto path2 = binaryCacheDir + "/" + path;
        static std::atomic<int> counter{0};
        Path tmp = fmt("%s.tmp.%d.%d", path2, getpid(), ++counter);
        AutoDelete del(tmp, false);
        StreamToSourceAdapter source(istream);
        writeFile(tmp, source);
        renameFile(tmp, path2);
        del.cancel();
        return {result::success()};
    } catch (...) {
        return {result::current_exception()};
    }

    kj::Promise<Result<box_ptr<AsyncInputStream>>>
    getFile(const std::string & path, const Activity * context) override
    try {
        try {
            return {
                make_box_ptr<AsyncGeneratorInputStream>(readFileSource(binaryCacheDir + "/" + path))
            };
        } catch (SysError & e) {
            if (e.errNo == ENOENT)
                throw NoSuchBinaryCacheFile("file '%s' does not exist in binary cache", path);
            throw;
        }
    } catch (...) {
        return {result::current_exception()};
    }

    kj::Promise<Result<StorePathSet>> queryAllValidPaths() override
    try {
        StorePathSet paths;

        for (auto & entry : readDirectory(binaryCacheDir)) {
            if (entry.name.size() != 40 ||
                !entry.name.ends_with(".narinfo"))
                continue;
            paths.insert(parseStorePath(
                    config_.storeDir + "/" + entry.name.substr(0, entry.name.size() - 8)
                    + "-" + MissingName));
        }

        co_return paths;
    } catch (...) {
        co_return result::current_exception();
    }

    kj::Promise<Result<std::optional<TrustedFlag>>> isTrustedClient() override
    {
        return {result::success(Trusted)};
    }
};

kj::Promise<Result<void>> LocalBinaryCacheStore::init()
try {
    createDirs(binaryCacheDir + "/nar");
    createDirs(binaryCacheDir + "/" + realisationsPrefix);
    if (config_.writeDebugInfo)
        createDirs(binaryCacheDir + "/debuginfo");
    createDirs(binaryCacheDir + "/log");
    TRY_AWAIT(BinaryCacheStore::init());
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

kj::Promise<Result<bool>>
LocalBinaryCacheStore::fileExists(const std::string & path, const Activity * context)
try {
    return {pathExists(binaryCacheDir + "/" + path)};
} catch (...) {
    return {result::current_exception()};
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
