#include "lix/libcmd/command.hh"
#include "derivation.hh"

namespace nix {

struct CmdDerivation : MultiCommand
{
    CmdDerivation() : MultiCommand(CommandRegistry::getCommandsFor({"derivation"}))
    { }

    std::string description() override
    {
        return "Work with derivations, Nix's notion of a build plan.";
    }

    Category category() override { return catUtility; }

    void run() override
    {
        if (!command)
            throw UsageError("'nix derivation' requires a sub-command.");
        command->second->run();
    }
};

void registerNixDerivation()
{
    registerCommand<CmdDerivation>("derivation");
}

}
