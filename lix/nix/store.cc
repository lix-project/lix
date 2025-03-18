#include "lix/libcmd/command.hh"
#include "store.hh"

namespace nix {

struct CmdStore : MultiCommand
{
    CmdStore() : MultiCommand(CommandRegistry::getCommandsFor({"store"}))
    { }

    std::string description() override
    {
        return "manipulate a Nix store";
    }

    Category category() override { return catUtility; }

    void run() override
    {
        if (!command)
            throw UsageError("'nix store' requires a sub-command.");
        command->second->run();
    }
};

void registerNixStore()
{
    registerCommand<CmdStore>("store");
}

}
