// FIXME: integrate this with nix path-info?
// FIXME: rename to 'nix store derivation show' or 'nix debug derivation show'?

#include "lix/libcmd/command.hh"
#include "lix/libmain/common-args.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libutil/archive.hh"
#include "lix/libutil/json.hh"
#include "lix/libstore/derivations.hh"

namespace nix {

struct CmdShowDerivation : InstallablesCommand
{
    bool recursive = false;

    CmdShowDerivation()
    {
        addFlag({
            .longName = "recursive",
            .shortName = 'r',
            .description = "Include the dependencies of the specified derivations.",
            .handler = {&recursive, true}
        });
    }

    std::string description() override
    {
        return "show the contents of a store derivation";
    }

    std::string doc() override
    {
        return
          #include "derivation-show.md"
          ;
    }

    Category category() override { return catUtility; }

    void run(ref<Store> store, Installables && installables) override
    {
        auto drvPaths =
            Installable::toDerivations(*getEvaluator()->begin(aio()), store, installables, true);

        if (recursive) {
            StorePathSet closure;
            aio().blockOn(store->computeFSClosure(drvPaths, closure));
            drvPaths = std::move(closure);
        }

        JSON jsonRoot = JSON::object();

        for (auto & drvPath : drvPaths) {
            if (!drvPath.isDerivation()) continue;

            jsonRoot[store->printStorePath(drvPath)] =
                aio().blockOn(store->readDerivation(drvPath)).toJSON(*store);
        }
        logger->cout(jsonRoot.dump(2));
    }
};

void registerNixDerivationShow()
{
    registerCommand2<CmdShowDerivation>({"derivation", "show"});
}

}
