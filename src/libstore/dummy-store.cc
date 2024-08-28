#include "dummy-store.hh"
#include "store-api.hh"

namespace nix {

struct DummyStoreConfig : virtual StoreConfig {
    using StoreConfig::StoreConfig;

    const std::string name() override { return "Dummy Store"; }

    std::string doc() override
    {
        return
          #include "dummy-store.md"
          ;
    }
};

struct DummyStore : public virtual DummyStoreConfig, public virtual Store
{
    DummyStore(const std::string scheme, const std::string uri, const Params & params)
        : DummyStore(params)
    { }

    DummyStore(const Params & params)
        : StoreConfig(params)
        , DummyStoreConfig(params)
        , Store(params)
    { }

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
