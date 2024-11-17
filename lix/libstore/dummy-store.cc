#include "lix/libstore/dummy-store.hh"
#include "lix/libstore/store-api.hh"

namespace nix {

struct DummyStoreConfig final : StoreConfig {
    using StoreConfig::StoreConfig;

    const std::string name() override { return "Dummy Store"; }

    std::string doc() override
    {
        return
          #include "dummy-store.md"
          ;
    }
};

struct DummyStore final : public Store
{
    DummyStoreConfig config_;

    DummyStoreConfig & config() override { return config_; }
    const DummyStoreConfig & config() const override { return config_; }

    DummyStore(const std::string scheme, const std::string uri, DummyStoreConfig config)
        : DummyStore(std::move(config))
    { }

    DummyStore(DummyStoreConfig config) : Store(config), config_(std::move(config)) {}

    std::string getUri() override
    {
        return *uriSchemes().begin();
    }

    std::shared_ptr<const ValidPathInfo> queryPathInfoUncached(const StorePath & path) override
    {
        return nullptr;
    }

    /**
     * The dummy store is incapable of *not* trusting! :)
     */
    virtual std::optional<TrustedFlag> isTrustedClient() override
    {
        return Trusted;
    }

    static std::set<std::string> uriSchemes() {
        return {"dummy"};
    }

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override
    { unsupported("queryPathFromHashPart"); }

    void addToStore(const ValidPathInfo & info, Source & source,
        RepairFlag repair, CheckSigsFlag checkSigs) override
    { unsupported("addToStore"); }

    StorePath addTextToStore(
        std::string_view name,
        std::string_view s,
        const StorePathSet & references,
        RepairFlag repair) override
    { unsupported("addTextToStore"); }

    WireFormatGenerator narFromPath(const StorePath & path) override
    { unsupported("narFromPath"); }

    std::shared_ptr<const Realisation> queryRealisationUncached(const DrvOutput &) override
    { return nullptr; }

    virtual ref<FSAccessor> getFSAccessor() override
    { unsupported("getFSAccessor"); }
};

void registerDummyStore() {
    StoreImplementations::add<DummyStore, DummyStoreConfig>();
}

}
