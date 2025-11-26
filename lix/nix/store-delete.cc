#include "lix/libcmd/command.hh"
#include "lix/libmain/common-args.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/store-cast.hh"
#include "lix/libstore/gc-store.hh"
#include "lix/libutil/file-system.hh"
#include "store-delete.hh"

namespace nix {

struct CmdStoreDelete : StorePathsCommand
{
    GCOptions options { .action = GCOptions::gcDeleteSpecific };
    bool deleteClosure = false;
    bool unlink = false;

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
        addFlag({
            .longName = "unlink",
            .description = "Unlink specified GC roots before deleting them",
            .handler = {&unlink, true},
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

        // If the user specified --unlink, try to remove any store-pointing symlinks specified
        // on the command-line.
        if (this->unlink) {
            for (auto const & path : parsePathsToUnlink(store)) {
                printTalkative("unlinking '%s'", path);
                deletePath(path);
            }
        }

        for (auto const & path : storePaths) {
            if (deleteClosure) {
                aio().blockOn(store->computeFSClosure(path, options.pathsToDelete));
            } else {
                options.pathsToDelete.insert(path);
            }
        }

        GCResults results;
        PrintFreed freed(options.action, results);
        aio().blockOn(gcStore.collectGarbage(options, results));
    }

    std::unordered_set<Path> parsePathsToUnlink(ref<Store> store) const
    {
        // Reaching into a distant base class because this C++ inheritance-centric command API can bite me.
        return this->rawInstallables
            | std::views::filter([store](std::string const & rawInst) {
                // Installables are never parsed as paths unless there's a slash.
                // We also only care about paths that aren't actually in the store...
                if (!rawInst.contains('/') || !isLink(rawInst) || store->isInStore(rawInst)) {
                    return false;
                }

                // ...just paths that *point* to the store.
                try {
                    [[maybe_unused]] Path const resolved = store->followLinksToStore(rawInst);
                    return true;
                } catch (BadStorePath const &) {
                    return false;
                }
            })
            | std::ranges::to<std::unordered_set>();
    }

};

void registerNixStoreDelete()
{
    registerCommand2<CmdStoreDelete>({"store", "delete"});
}

}
