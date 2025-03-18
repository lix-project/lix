#include "lix/libcmd/command.hh"
#include "nar.hh"

namespace nix {

struct CmdNar : MultiCommand
{
    CmdNar() : MultiCommand(CommandRegistry::getCommandsFor({"nar"}))
    { }

    std::string description() override
    {
        return "create or inspect NAR files";
    }

    std::string doc() override
    {
        return
          #include "nar.md"
          ;
    }

    Category category() override { return catUtility; }

    void run() override
    {
        if (!command)
            throw UsageError("'nix nar' requires a sub-command.");
        command->second->run();
    }
};

void registerNixNar()
{
    registerCommand<CmdNar>("nar");
}

}
