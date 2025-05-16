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
        throw UnimplementedError("CA derivations are no longer supported");
    }
};

void registerNixRealisation()
{
    registerCommand<CmdRealisation>("realisation");
    registerCommand2<CmdRealisationInfo>({"realisation", "info"});
}

}
