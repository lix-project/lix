#include "lix/libcmd/command.hh"
#include "lix/libmain/common-args.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/store-cast.hh"
#include "lix/libstore/gc-store.hh"

using namespace nix;

struct CmdStoreDelete : StorePathsCommand
{
    GCOptions options { .action = GCOptions::gcDeleteSpecific };

    CmdStoreDelete()
    {
        addFlag({
            .longName = "ignore-liveness",
            .description = "Do not check whether the paths are reachable from a root.",
            .handler = {&options.ignoreLiveness, true}
        });
    }

    std::string description() override
    {
        return "delete paths from the Nix store";
    }

    std::string doc() override
    {
        return
          #include "store-delete.md"
          ;
    }

    void run(ref<Store> store, StorePaths && storePaths) override
    {
        auto & gcStore = require<GcStore>(*store);

        for (auto & path : storePaths)
            options.pathsToDelete.insert(path);

        GCResults results;
        PrintFreed freed(true, results);
        gcStore.collectGarbage(options, results);
    }
};

static auto rCmdStoreDelete = registerCommand2<CmdStoreDelete>({"store", "delete"});
