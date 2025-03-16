#include "lix/libcmd/command.hh"
#include "lix/libstore/store-api.hh"
#include "path-from-hash-part.hh"

namespace nix {

struct CmdPathFromHashPart : StoreCommand
{
    std::string hashPart;

    CmdPathFromHashPart()
    {
        expectArgs({
            .label = "hash-part",
            .handler = {&hashPart},
        });
    }

    std::string description() override
    {
        return "get a store path from its hash part";
    }

    std::string doc() override
    {
        return
          #include "path-from-hash-part.md"
          ;
    }

    void run(ref<Store> store) override
    {
        if (auto storePath = aio().blockOn(store->queryPathFromHashPart(hashPart)))
            logger->cout(store->printStorePath(*storePath));
        else
            throw Error("there is no store path corresponding to '%s'", hashPart);
    }
};

void registerNixStorePathFromHashPart()
{
    registerCommand2<CmdPathFromHashPart>({"store", "path-from-hash-part"});
}

}
