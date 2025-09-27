#include "lix/libcmd/installable-flake.hh"
#include "lix/libcmd/command.hh"
#include "lix/libexpr/eval-settings.hh"
#include "lix/libmain/common-args.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/local-fs-store.hh"
#include "lix/libstore/fs-accessor.hh"
#include "lix/libexpr/eval-inline.hh"
#include "bundle.hh"

namespace nix {

struct CmdBundle : InstallableCommand
{
    std::string bundler = "github:NixOS/bundlers";
    std::optional<Path> outLink;

    CmdBundle()
    {
        addFlag({
            .longName = "bundler",
            .description = fmt("Use a custom bundler instead of the default (`%s`).", bundler),
            .labels = {"flake-url"},
            .handler = {&bundler},
            .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
                completeFlakeRef(aio(), completions, getStore(), prefix);
            }}
        });

        addFlag({
            .longName = "out-link",
            .shortName = 'o',
            .description = "Override the name of the symlink to the build result. It defaults to the base name of the app.",
            .labels = {"path"},
            .handler = {&outLink},
            .completer = completePath
        });

    }

    std::string description() override
    {
        return "bundle an application so that it works outside of the Nix store";
    }

    std::string doc() override
    {
        return
          #include "bundle.md"
          ;
    }

    Category category() override { return catSecondary; }

    // FIXME: cut&paste from CmdRun.
    Strings getDefaultFlakeAttrPaths() override
    {
        // eval-system, since the app could be remote built and then bundled locally
        Strings res{
            "apps." + evalSettings.getCurrentSystem() + ".default",
            "defaultApp." + evalSettings.getCurrentSystem()
        };
        for (auto & s : SourceExprCommand::getDefaultFlakeAttrPaths())
            res.push_back(s);
        return res;
    }

    Strings getDefaultFlakeAttrPathPrefixes() override
    {
        // eval-system, since the app could be remote built and then bundled locally
        Strings res{"apps." + evalSettings.getCurrentSystem() + "."};
        for (auto & s : SourceExprCommand::getDefaultFlakeAttrPathPrefixes())
            res.push_back(s);
        return res;
    }

    void run(ref<Store> store, ref<Installable> installable) override
    {
        auto evaluator = getEvaluator();
        auto evalState = evaluator->begin(aio());

        auto const installableValue = InstallableValue::require(installable);

        auto val = installableValue->toValue(*evalState).first;

        auto [bundlerFlakeRef, bundlerName, extendedOutputsSpec] = parseFlakeRefWithFragmentAndExtendedOutputsSpec(bundler, absPath("."));
        const flake::LockFlags lockFlags{ .writeLockFile = false };
        // Current system, since it needs to run locally
        InstallableFlake bundler{this,
            evaluator, std::move(bundlerFlakeRef), bundlerName, std::move(extendedOutputsSpec),
            {"bundlers." + settings.thisSystem.get() + ".default",
             "defaultBundler." + settings.thisSystem.get()
            },
            {"bundlers." + settings.thisSystem.get() + "."},
            lockFlags
        };

        Value vRes;
        auto fn = bundler.toValue(*evalState).first;
        evalState->callFunction(fn, val, vRes, noPos);

        if (!evalState->isDerivation(vRes)) {
            throw Error("the bundler '%s' does not produce a derivation", bundler.what());
        }

        auto attr1 = vRes.attrs()->get(evaluator->s.drvPath);
        if (!attr1)
            throw Error("the bundler '%s' does not produce a derivation", bundler.what());

        NixStringContext context2;
        auto drvPath = evalState->coerceToStorePath(attr1->pos, attr1->value, context2, "");

        auto attr2 = vRes.attrs()->get(evaluator->s.outPath);
        if (!attr2)
            throw Error("the bundler '%s' does not produce a derivation", bundler.what());

        auto outPath = evalState->coerceToStorePath(attr2->pos, attr2->value, context2, "");

        aio().blockOn(store->buildPaths({
            DerivedPath::Built {
                .drvPath = makeConstantStorePath(drvPath),
                .outputs = OutputsSpec::All { },
            },
        }));

        if (!outLink) {
            auto * attr = vRes.attrs()->get(evaluator->s.name);
            if (!attr)
                throw Error("attribute 'name' missing");
            outLink = evalState->forceStringNoCtx(attr->value, attr->pos, "");
        }

        // TODO: will crash if not a localFSStore?
        aio().blockOn(
            store.try_cast_shared<LocalFSStore>()->addPermRoot(outPath, absPath(*outLink))
        );
    }
};

void registerNixBundle()
{
    registerCommand<CmdBundle>("bundle");
}

}
