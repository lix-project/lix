#include "lix/libcmd/command.hh"
#include "lix/libmain/common-args.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/store-cast.hh"
#include "lix/libstore/gc-store.hh"
#include "store-delete.hh"

namespace nix {

struct CmdStoreDelete : StorePathsCommand
{
    GCOptions options { .action = GCOptions::gcDeleteSpecific };
    bool deleteClosure = false;

    CmdStoreDelete()
    {
        addFlag({
            .longName = "ignore-liveness",
            .description = "Do not check whether the paths are reachable from a root.",
            .handler = {&options.ignoreLiveness, true}
        });
        addFlag({
            .longName = "skip-live",
            .description = "Skip deleting any paths that are reachable from a root.",
            .handler = {&options.action, GCOptions::gcTryDeleteSpecific}
        });
        addFlag({
            .longName = "delete-closure",
            .description = "Also attempt to delete all paths in the given paths' closures.",
            .handler = {&deleteClosure, true}
        });
        realiseMode = Realise::Nothing;
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

        for (auto & path : storePaths) {
            if (deleteClosure) {
                aio().blockOn(store->computeFSClosure(path, options.pathsToDelete));
            } else {
                options.pathsToDelete.insert(path);
            }
        }

        GCResults results;
        PrintFreed freed(true, results);
        aio().blockOn(gcStore.collectGarbage(options, results));
    }
};

void registerNixStoreDelete()
{
    registerCommand2<CmdStoreDelete>({"store", "delete"});
}

}
