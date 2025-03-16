#include "lix/libcmd/command.hh"
#include "lix/libstore/store-api.hh"
#include "store-repair.hh"

namespace nix {

struct CmdStoreRepair : StorePathsCommand
{
    std::string description() override
    {
        return "repair store paths";
    }

    std::string doc() override
    {
        return
          #include "store-repair.md"
          ;
    }

    void run(ref<Store> store, StorePaths && storePaths) override
    {
        for (auto & path : storePaths)
            aio().blockOn(store->repairPath(path));
    }
};

void registerNixStoreRepair()
{
    registerCommand2<CmdStoreRepair>({"store", "repair"});
}

}
