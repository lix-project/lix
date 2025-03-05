#include "lix/libcmd/command.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/finally.hh"

#include <nlohmann/json.hpp>

using namespace nix;

struct CmdPingStore : StoreCommand, MixJSON
{
    std::string description() override
    {
        return "test whether a store can be accessed";
    }

    std::string doc() override
    {
        return
          #include "ping-store.md"
          ;
    }

    void run(ref<Store> store) override
    {
        if (!json) {
            notice("Store URL: %s", store->getUri());
            aio().blockOn(store->connect());
            if (auto version = aio().blockOn(store->getVersion()))
                notice("Version: %s", *version);
            if (auto trusted = aio().blockOn(store->isTrustedClient()))
                notice("Trusted: %s", *trusted);
        } else {
            nlohmann::json res;
            Finally printRes([&]() {
                logger->cout("%s", res);
            });

            res["url"] = store->getUri();
            aio().blockOn(store->connect());
            if (auto version = aio().blockOn(store->getVersion()))
                res["version"] = *version;
            if (auto trusted = aio().blockOn(store->isTrustedClient()))
                res["trusted"] = *trusted;
        }
    }
};

static auto rCmdPingStore = registerCommand2<CmdPingStore>({"store", "ping"});
