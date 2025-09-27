#include "lix/libcmd/command.hh"
#include "lix/libmain/shared.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/attr-path.hh"
#include "lix/libcmd/editor-for.hh"
#include "lix/libutil/current-process.hh"
#include "edit.hh"

#include <unistd.h>

namespace nix {

struct CmdEdit : InstallableCommand
{
    std::string description() override
    {
        return "open the Nix expression of a Nix package in $EDITOR";
    }

    std::string doc() override
    {
        return
          #include "edit.md"
          ;
    }

    Category category() override { return catSecondary; }

    void run(ref<Store> store, ref<Installable> installable) override
    {
        auto evaluator = getEvaluator();
        auto state = evaluator->begin(aio());

        auto const installableValue = InstallableValue::require(installable);

        const auto [file, line] = [&] {
            auto [v, pos] = installableValue->toValue(*state);

            try {
                return findPackageFilename(*state, v, installable->what());
            } catch (NoPositionInfo &) {
                throw Error("cannot find position information for '%s", installableValue->what());
            }
        }();

        logger->pause();

        auto args = editorFor(file, line);

        restoreProcessContext();

        printMsg(lvlChatty, "running editor: %s", concatMapStringsSep(" ", args, shellEscape));

        execvp(args.front().c_str(), stringsToCharPtrs(args).data());

        std::string command;
        for (const auto &arg : args) command += " '" + arg + "'";
        throw SysError("cannot run command%s", command);
    }
};

void registerNixEdit()
{
    registerCommand<CmdEdit>("edit");
}

}
