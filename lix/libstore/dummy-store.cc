#include "lix/libstore/dummy-store.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/async-io.hh"

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

    DummyStore(DummyStoreConfig config) : Store(config), config_(std::move(config)) {}

    kj::Promise<Result<std::optional<ref<Store>>>> static open(
        const std::string &, const Path &, DummyStoreConfig config
    )
    try {
        co_return make_ref<DummyStore>(std::move(config));
    } catch (...) {
        co_return result::current_exception();
    }

    std::string getUri() override
    {
        return scheme;
    }

    kj::Promise<Result<std::shared_ptr<const ValidPathInfo>>>
    queryPathInfoUncached(const StorePath & path, const Activity * context) override
    {
        return {result::success(nullptr)};
    }

    /**
     * The dummy store is incapable of *not* trusting! :)
     */
    virtual kj::Promise<Result<std::optional<TrustedFlag>>> isTrustedClient() override
    {
        return {result::success(Trusted)};
    }

    static inline const std::string scheme = "dummy";

    kj::Promise<Result<std::optional<StorePath>>>
    queryPathFromHashPart(const std::string & hashPart) override
    try {
        unsupported("queryPathFromHashPart");
    } catch (...) {
        return {result::current_exception()};
    }

    kj::Promise<Result<void>> addToStore(
        const ValidPathInfo & info,
        AsyncInputStream & source,
        RepairFlag repair,
        CheckSigsFlag checkSigs,
        const Activity * context
    ) override
    try {
        unsupported("addToStore");
    } catch (...) {
        return {result::current_exception()};
    }

    kj::Promise<Result<StorePath>> addTextToStore(
        std::string_view name,
        std::string_view s,
        const StorePathSet & references,
        RepairFlag repair) override
    try { unsupported("addTextToStore"); } catch (...) { return {result::current_exception()}; }

    kj::Promise<Result<box_ptr<AsyncInputStream>>>
    narFromPath(const StorePath & path, const Activity * context) override
    try {
        unsupported("narFromPath");
    } catch (...) {
        return {result::current_exception()};
    }

    virtual ref<FSAccessor> getFSAccessor() override
    { unsupported("getFSAccessor"); }
};

void registerDummyStore() {
    StoreImplementations::add<DummyStore, DummyStoreConfig>({DummyStore::scheme});
}

}
