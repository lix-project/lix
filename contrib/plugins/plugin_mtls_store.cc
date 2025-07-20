#include "lix/libstore/store-api.hh"
#include "lix/libutil/config.hh"
#include "lix/libstore/http-binary-cache-store.hh"
#include <stdlib.h>
#include <curl/curl.h>

namespace nix {
struct mTLSBinaryCacheStoreConfig : HttpBinaryCacheStoreConfig
{
    using HttpBinaryCacheStoreConfig::HttpBinaryCacheStoreConfig;

    const std::string name() override
    {
        return "mTLS HTTP Binary Cache Store";
    }

    std::string doc() override
    {
        return
#include "mtls-http-binary-cache-store.md"
            ;
    }

    PathsSetting<nix::Path> tlsCertificate{
        this,
        "",
        "tls-certificate",
        "Path of an optional TLS client certificate in PEM format as expected by CURLOPT_SSLCERT"
    };

    PathsSetting<nix::Path> tlsKey{
        this,
        "",
        "tls-private-key",
        "Path of an TLS client certificate private key in PEM format as expected by CURLOPT_SSLKEY"
    };
};

struct mTLSBinaryCacheStoreImpl : public HttpBinaryCacheStore
{
    struct Keyring
    {
        nix::Path tlsCertificate;
        nix::Path tlsKey;
    };

    mTLSBinaryCacheStoreConfig config_;
    std::shared_ptr<Keyring> keyring;

    mTLSBinaryCacheStoreConfig & config() override
    {
        return config_;
    }
    const mTLSBinaryCacheStoreConfig & config() const override
    {
        return config_;
    }

    mTLSBinaryCacheStoreImpl(
        const std::string & uriScheme, const Path & _cacheUri, mTLSBinaryCacheStoreConfig config
    )
        : Store(config)
        , HttpBinaryCacheStore("https", _cacheUri, config)
        , config_(std::move(config))
        , keyring(std::make_shared<Keyring>(config_.tlsCertificate.get(), config_.tlsKey.get()))
    {
    }

    FileTransferOptions makeOptions(Headers && headers = {}) override
    {
        auto options = HttpBinaryCacheStore::makeOptions(std::move(headers));
        auto baseExtraSetup = std::move(options.extraSetup);
        auto keyring = this->keyring;

        options.extraSetup =
            [keyring, baseExtraSetup{std::move(baseExtraSetup)}](CURL * req) {
                if (baseExtraSetup) {
                    baseExtraSetup(req);
                }

                if (!keyring->tlsCertificate.empty()) {
                    curl_easy_setopt(req, CURLOPT_SSLCERT, keyring->tlsCertificate.c_str());
                }

                curl_easy_setopt(req, CURLOPT_SSLKEY, keyring->tlsKey.c_str());
            };

        return options;
    }

    static std::set<std::string> uriSchemes()
    {
        return {"https+mtls"};
    }
};
}


extern "C" void nix_plugin_entry()
{
    nix::StoreImplementations::add<nix::mTLSBinaryCacheStoreImpl, nix::mTLSBinaryCacheStoreConfig>();
}
