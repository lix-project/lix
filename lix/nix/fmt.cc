#include "lix/libcmd/command.hh"
#include "lix/libcmd/installable-value.hh"
#include "run.hh"
#include "fmt.hh"

namespace nix {

struct CmdFmt : SourceExprCommand {
    std::vector<std::string> args;

    CmdFmt() { expectArgs({.label = "args", .handler = {&args}}); }

    std::string description() override {
        return "reformat your code in the standard style";
    }

    std::string doc() override {
        return
          #include "fmt.md"
          ;
    }

    Category category() override { return catSecondary; }

    Strings getDefaultFlakeAttrPaths() override {
        // We are running it locally so it should be the actual system
        return Strings{"formatter." + settings.thisSystem.get()};
    }

    Strings getDefaultFlakeAttrPathPrefixes() override { return Strings{}; }

    void run(ref<Store> store) override
    {
        auto evaluator = getEvaluator();
        auto evalStore = getEvalStore();
        auto state = evaluator->begin(aio());

        auto installable_ = parseInstallable(*state, store, ".");
        auto & installable = InstallableValue::require(*installable_);
        auto app = installable.toApp(*state).resolve(*state, evalStore, store);

        Strings programArgs{app.program};

        // Propagate arguments from the CLI
        for (auto &i : args) {
            programArgs.push_back(i);
        }

        runProgramInStore(store, UseSearchPath::DontUse, app.program, programArgs);
    };
};

void registerNixFmt()
{
    registerCommand<CmdFmt>("fmt");
}

}
