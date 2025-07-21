#pragma once
///@file

#include "lix/libstore/binary-cache-store.hh"
#include "lix/libstore/filetransfer.hh"

#include <curl/curl.h>

namespace nix {

struct HttpBinaryCacheStoreConfig : BinaryCacheStoreConfig
{
    using BinaryCacheStoreConfig::BinaryCacheStoreConfig;

    const std::string name() override
    {
        return "HTTP Binary Cache Store";
    }

    std::string doc() override;
};

class HttpBinaryCacheStore : public BinaryCacheStore
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
        const std::string & scheme, const Path & _cacheUri, HttpBinaryCacheStoreConfig config
    );

    HttpBinaryCacheStoreConfig & config() override
    {
        return config_;
    }
    const HttpBinaryCacheStoreConfig & config() const override
    {
        return config_;
    }

    std::string getUri() override
    {
        return cacheUri;
    }

    kj::Promise<Result<void>> init() override;

    /** Override this to configure additional curl options on the request.
     * e.g. authentication method or key material.
     *
     * This is meant mostly for plugins who wish to perform their own
     * custom initialization.
     */
    virtual FileTransferOptions makeOptions(Headers && headers = {});

    static std::set<std::string> uriSchemes()
    {
        static bool forceHttp = getEnv("_NIX_FORCE_HTTP") == "1";
        auto ret = std::set<std::string>({"http", "https"});
        if (forceHttp) {
            ret.insert("file");
        }
        return ret;
    }

protected:

    void maybeDisable();
    void checkEnabled();

    kj::Promise<Result<bool>> fileExists(const std::string & path, const Activity * context) override;
    kj::Promise<Result<void>> upsertFile(
        const std::string & path,
        std::shared_ptr<std::basic_iostream<char>> istream,
        const std::string & mimeType,
        const Activity * context
    ) override;
    kj::Promise<Result<box_ptr<AsyncInputStream>>> getFile(const std::string & path, const Activity * context) override;

    std::string makeURI(const std::string & path)
    {
        return path.starts_with("https://") || path.starts_with("http://")
                || path.starts_with("file://")
            ? path
            : cacheUri + "/" + path;
    }

    /**
     * This isn't actually necessary read only. We support "upsert" now, so we
     * have a notion of authentication via HTTP POST/PUT.
     *
     * For now, we conservatively say we don't know.
     *
     * \todo try to expose our HTTP authentication status.
     */
    kj::Promise<Result<std::optional<TrustedFlag>>> isTrustedClient() override
    {
        return {result::success(std::nullopt)};
    }
};

void registerHttpBinaryCacheStore();

}
