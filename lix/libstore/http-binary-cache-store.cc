#include "lix/libstore/http-binary-cache-store.hh"
#include "lix/libstore/binary-cache-store.hh"
#include "lix/libstore/filetransfer.hh"
#include "lix/libstore/globals.hh"
#include "lix/libstore/nar-info-disk-cache.hh"

namespace nix {

MakeError(UploadToHTTP, Error);

struct HttpBinaryCacheStoreConfig final : BinaryCacheStoreConfig
{
    using BinaryCacheStoreConfig::BinaryCacheStoreConfig;

    const std::string name() override { return "HTTP Binary Cache Store"; }

    std::string doc() override
    {
        return
          #include "http-binary-cache-store.md"
          ;
    }
};

class HttpBinaryCacheStore final : public BinaryCacheStore
{
private:

    HttpBinaryCacheStoreConfig config_;
    Path cacheUri;

    struct State
    {
        bool enabled = true;
        std::chrono::steady_clock::time_point disabledUntil;
    };

    Sync<State> _state;

public:

    HttpBinaryCacheStore(
        const std::string & scheme,
        const Path & _cacheUri,
        HttpBinaryCacheStoreConfig config)
        : Store(config)
        , BinaryCacheStore(config)
        , config_(std::move(config))
        , cacheUri(scheme + "://" + _cacheUri)
    {
        if (cacheUri.back() == '/')
            cacheUri.pop_back();

        diskCache = getNarInfoDiskCache();
    }

    HttpBinaryCacheStoreConfig & config() override { return config_; }
    const HttpBinaryCacheStoreConfig & config() const override { return config_; }

    std::string getUri() override
    {
        return cacheUri;
    }

    void init() override
    {
        // FIXME: do this lazily?
        if (auto cacheInfo = diskCache->upToDateCacheExists(cacheUri)) {
            config_.wantMassQuery.setDefault(cacheInfo->wantMassQuery);
            config_.priority.setDefault(cacheInfo->priority);
        } else {
            try {
                BinaryCacheStore::init();
            } catch (UploadToHTTP &) {
                throw Error("'%s' does not appear to be a binary cache", cacheUri);
            }
            diskCache->createCache(
                cacheUri, config_.storeDir, config_.wantMassQuery, config_.priority
            );
        }
    }

    static std::set<std::string> uriSchemes()
    {
        static bool forceHttp = getEnv("_NIX_FORCE_HTTP") == "1";
        auto ret = std::set<std::string>({"http", "https"});
        if (forceHttp) ret.insert("file");
        return ret;
    }

protected:

    void maybeDisable()
    {
        auto state(_state.lock());
        if (state->enabled && settings.tryFallback) {
            int t = 60;
            printError("disabling binary cache '%s' for %s seconds", getUri(), t);
            state->enabled = false;
            state->disabledUntil = std::chrono::steady_clock::now() + std::chrono::seconds(t);
        }
    }

    void checkEnabled()
    {
        auto state(_state.lock());
        if (state->enabled) return;
        if (std::chrono::steady_clock::now() > state->disabledUntil) {
            state->enabled = true;
            debug("re-enabling binary cache '%s'", getUri());
            return;
        }
        throw SubstituterDisabled("substituter '%s' is disabled", getUri());
    }

    bool fileExists(const std::string & path) override
    {
        checkEnabled();

        try {
            return getFileTransfer()->exists(makeURI(path));
        } catch (FileTransferError & e) {
            maybeDisable();
            throw;
        }
    }

    void upsertFile(const std::string & path,
        std::shared_ptr<std::basic_iostream<char>> istream,
        const std::string & mimeType) override
    {
        auto data = StreamToSourceAdapter(istream).drain();
        try {
            getFileTransfer()->upload(makeURI(path), std::move(data), {{"Content-Type", mimeType}});
        } catch (FileTransferError & e) {
            throw UploadToHTTP(
                "while uploading to HTTP binary cache at '%s': %s", cacheUri, e.msg()
            );
        }
    }

    std::string makeURI(const std::string & path)
    {
        return path.starts_with("https://") || path.starts_with("http://")
                || path.starts_with("file://")
            ? path
            : cacheUri + "/" + path;
    }

    box_ptr<Source> getFile(const std::string & path) override
    {
        checkEnabled();
        try {
            return getFileTransfer()->download(makeURI(path)).second;
        } catch (FileTransferError & e) {
            if (e.error == FileTransfer::NotFound || e.error == FileTransfer::Forbidden)
                throw NoSuchBinaryCacheFile("file '%s' does not exist in binary cache '%s'", path, getUri());
            maybeDisable();
            throw;
        }
    }

    std::optional<std::string> getFileContents(const std::string & path) override
    {
        checkEnabled();

        try {
            return getFileTransfer()->download(makeURI(path)).second->drain();
        } catch (FileTransferError & e) {
            if (e.error == FileTransfer::NotFound || e.error == FileTransfer::Forbidden)
                return {};
            maybeDisable();
            throw;
        }
    }

    /**
     * This isn't actually necessary read only. We support "upsert" now, so we
     * have a notion of authentication via HTTP POST/PUT.
     *
     * For now, we conservatively say we don't know.
     *
     * \todo try to expose our HTTP authentication status.
     */
    std::optional<TrustedFlag> isTrustedClient() override
    {
        return std::nullopt;
    }
};

void registerHttpBinaryCacheStore() {
    StoreImplementations::add<HttpBinaryCacheStore, HttpBinaryCacheStoreConfig>();
}

}
