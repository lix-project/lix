#include "lix/libcmd/command.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/path.hh"
#include "lix/libstore/store-api.hh"
#include "copy.hh"

namespace nix {

struct CmdCopy : virtual CopyCommand, StorePathsCommand
{
    CheckSigsFlag checkSigs = CheckSigs;

    SubstituteFlag substitute = NoSubstitute;

    CmdCopy() : StorePathsCommand(true)
    {
        addFlag({
            .longName = "no-check-sigs",
            .description = "Do not require that paths are signed by trusted keys.",
            .handler = {&checkSigs, NoCheckSigs},
        });

        addFlag({
            .longName = "substitute-on-destination",
            .shortName = 's',
            .description =
                "Whether to try substitutes on the destination store (only supported by SSH stores).",
            .handler = {&substitute, Substitute},
        });
    }

    std::string description() override
    {
        return "copy paths between Nix stores";
    }

    std::string doc() override
    {
        return
          #include "copy.md"
          ;
    }

    Category category() override { return catSecondary; }

    void run(ref<Store> srcStore, StorePaths && paths) override
    {
        auto dstStore = getDstStore();

        RealisedPath::Set stuffToCopy;

        for (auto & path : paths) {
            stuffToCopy.emplace(path);
        }

        aio().blockOn(copyPaths(
            *srcStore, *dstStore, stuffToCopy, NoRepair, checkSigs, substitute));
    }
};

void registerNixCopy()
{
    registerCommand<CmdCopy>("copy");
}

}
