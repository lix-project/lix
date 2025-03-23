#include "lix/libcmd/command.hh"
#include "lix/libmain/common-args.hh"
#include "lix/libutil/json.hh"
#include "realisation.hh"

namespace nix {

struct CmdRealisation : MultiCommand
{
    CmdRealisation() : MultiCommand(CommandRegistry::getCommandsFor({"realisation"}))
    { }

    std::string description() override
    {
        return "manipulate a Nix realisation";
    }

    Category category() override { return catUtility; }

    void run() override
    {
        if (!command)
            throw UsageError("'nix realisation' requires a sub-command.");
        command->second->run();
    }
};

struct CmdRealisationInfo : BuiltPathsCommand, MixJSON
{
    CmdRealisationInfo() : BuiltPathsCommand(false) {}

    std::string description() override
    {
        return "query information about one or several realisations";
    }

    std::string doc() override
    {
        return
            #include "realisation/info.md"
            ;
    }

    Category category() override { return catSecondary; }

    void run(ref<Store> store, BuiltPaths && paths) override
    {
        experimentalFeatureSettings.require(Xp::CaDerivations);
        RealisedPath::Set realisations;

        for (auto & builtPath : paths) {
            auto theseRealisations = aio().blockOn(builtPath.toRealisedPaths(*store));
            realisations.insert(theseRealisations.begin(), theseRealisations.end());
        }

        if (json) {
            nlohmann::json res = nlohmann::json::array();
            for (auto & path : realisations) {
                nlohmann::json currentPath;
                if (auto realisation = std::get_if<Realisation>(&path.raw))
                    currentPath = realisation->toJSON();
                else
                    currentPath["opaquePath"] = store->printStorePath(path.path());

                res.push_back(currentPath);
            }
            logger->cout("%s", res);
        }
        else {
            for (auto & path : realisations) {
                if (auto realisation = std::get_if<Realisation>(&path.raw)) {
                    logger->cout("%s %s",
                        realisation->id.to_string(),
                        store->printStorePath(realisation->outPath));
                } else
                    logger->cout("%s", store->printStorePath(path.path()));
            }
        }
    }
};

void registerNixRealisation()
{
    registerCommand<CmdRealisation>("realisation");
    registerCommand2<CmdRealisationInfo>({"realisation", "info"});
}

}
