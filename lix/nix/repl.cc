#include "lix/libexpr/eval.hh"
#include "lix/libexpr/eval-settings.hh"
#include "lix/libstore/globals.hh"
#include "lix/libcmd/command.hh"
#include "lix/libcmd/installable-value.hh"
#include "lix/libcmd/repl.hh"
#include "repl.hh"

namespace nix {

struct CmdRepl : RawInstallablesCommand
{
    CmdRepl() {
        evalSettings.pureEval.override(false);
    }

    /**
     * This command is stable before the others
     */
    std::optional<ExperimentalFeature> experimentalFeature() override
    {
        return std::nullopt;
    }

    std::vector<std::string> files;

    Strings getDefaultFlakeAttrPaths() override
    {
        return {""};
    }

    bool forceImpureByDefault() override
    {
        return true;
    }

    std::string description() override
    {
        return "start an interactive environment for evaluating Nix expressions";
    }

    std::string doc() override
    {
        return
          #include "repl.md"
          ;
    }

    void applyDefaultInstallables(std::vector<std::string> & rawInstallables) override
    {
        if (rawInstallables.empty() && (file.has_value() || expr.has_value())) {
            rawInstallables.push_back(".");
        }
    }

    void run(ref<Store> store, std::vector<std::string> && rawInstallables) override
    {
        auto evaluator = getEvaluator();
        auto state = evaluator->begin(aio());
        auto getValues = [&]()->AbstractNixRepl::AnnotatedValues{
            auto installables = parseInstallables(*state, store, rawInstallables);
            AbstractNixRepl::AnnotatedValues values;
            for (auto & installable_: installables){
                auto & installable = InstallableValue::require(*installable_);
                auto what = installable.what();
                if (file){
                    auto [val, pos] = installable.toValue(*state);
                    auto what = installable.what();
                    state->forceValue(val, pos);
                    auto autoArgs = getAutoArgs(*evaluator);
                    Value valPost;
                    state->autoCallFunction(*autoArgs, val, valPost, pos);
                    state->forceValue(valPost, pos);
                    values.push_back( {valPost, what });
                } else {
                    auto [val, pos] = installable.toValue(*state);
                    values.push_back({val, what});
                }
            }
            return values;
        };
        AbstractNixRepl::run(
            searchPath, aio().blockOn(openStore()), *state, getValues, {}, getAutoArgs(*evaluator)
        );
    }
};

void registerNixRepl()
{
    registerCommand<CmdRepl>("repl");
}

}
