#include "lix/libcmd/command.hh"
#include "lix/libmain/common-args.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/store-cast.hh"
#include "lix/libstore/gc-store.hh"
#include "store-gc.hh"

namespace nix {

struct CmdStoreGC : StoreCommand, MixDryRun
{
    GCOptions options;

    CmdStoreGC()
    {
        addFlag({
            .longName = "max",
            .description = "Stop after freeing *n* bytes of disk space.",
            .labels = {"n"},
            .handler = {&options.maxFreed}
        });
    }

    std::string description() override
    {
        return "perform garbage collection on a Nix store";
    }

    std::string doc() override
    {
        return
          #include "store-gc.md"
          ;
    }

    void run(ref<Store> store) override
    {
        auto & gcStore = require<GcStore>(*store);

        options.action = dryRun ? GCOptions::gcReturnDead : GCOptions::gcDeleteDead;
        GCResults results;
        PrintFreed freed(options.action == GCOptions::gcDeleteDead, results);
        aio().blockOn(gcStore.collectGarbage(options, results));
    }
};

void registerNixStoreGc()
{
    registerCommand2<CmdStoreGC>({"store", "gc"});
}

}
