#include "lix/libcmd/command.hh"
#include "lix/libmain/shared.hh"
#include "lix/libutil/signals.hh"
#include "lix/libstore/store-api.hh"
#include "optimise-store.hh"

#include <atomic>

namespace nix {

struct CmdOptimiseStore : StoreCommand
{
    std::string description() override
    {
        return "replace identical files in the store by hard links";
    }

    std::string doc() override
    {
        return
          #include "optimise-store.md"
          ;
    }

    void run(ref<Store> store) override
    {
        aio().blockOn(store->optimiseStore());
    }
};

void registerNixStoreOptimise()
{
    registerCommand2<CmdOptimiseStore>({"store", "optimise"});
}

}
