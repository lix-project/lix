// FIXME: rename to 'nix plan add' or 'nix derivation add'?

#include "lix/libcmd/command.hh"
#include "lix/libmain/common-args.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/archive.hh"
#include "lix/libutil/json.hh"
#include "lix/libstore/derivations.hh"

namespace nix {

struct CmdAddDerivation : MixDryRun, StoreCommand
{
    std::string description() override
    {
        return "Add a store derivation";
    }

    std::string doc() override
    {
        return
          #include "derivation-add.md"
          ;
    }

    Category category() override { return catUtility; }

    void run(ref<Store> store) override
    {
        auto json = json::parse(drainFD(STDIN_FILENO), "a derivation from stdin");

        auto drv = Derivation::fromJSON(*store, json);

        auto drvPath = aio().blockOn(writeDerivation(*store, drv, NoRepair, /* read only */ dryRun));

        aio().blockOn(drv.checkInvariants(*store, drvPath));

        aio().blockOn(writeDerivation(*store, drv, NoRepair, dryRun));

        logger->cout("%s", store->printStorePath(drvPath));
    }
};

void registerNixDerivationAdd()
{
    registerCommand2<CmdAddDerivation>({"derivation", "add"});
}

}
