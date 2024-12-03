#include "lix/libcmd/installables.hh"
#include "lix/libcmd/installable-derived-path.hh"
#include "lix/libcmd/installable-value.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libexpr/eval-inline.hh"
#include "lix/libexpr/eval-cache.hh"
#include "lix/libstore/names.hh"
#include "lix/libcmd/command.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libstore/downstream-placeholder.hh"

namespace nix {

/**
 * Return the rewrites that are needed to resolve a string whose context is
 * included in `dependencies`.
 */
StringPairs resolveRewrites(
    Store & store,
    const std::vector<BuiltPathWithResult> & dependencies)
{
    StringPairs res;
    if (experimentalFeatureSettings.isEnabled(Xp::CaDerivations)) {
        for (auto & dep : dependencies) {
            if (auto drvDep = std::get_if<BuiltPathBuilt>(&dep.path)) {
                for (auto & [ outputName, outputPath ] : drvDep->outputs) {
                    res.emplace(
                        DownstreamPlaceholder::fromSingleDerivedPathBuilt(
                            SingleDerivedPath::Built {
                                .drvPath = make_ref<SingleDerivedPath>(drvDep->drvPath->discardOutputPath()),
                                .output = outputName,
                            }).render(),
                        store.printStorePath(outputPath)
                    );
                }
            }
        }
    }
    return res;
}

/**
 * Resolve the given string assuming the given context.
 */
std::string resolveString(
    Store & store,
    const std::string & toResolve,
    const std::vector<BuiltPathWithResult> & dependencies)
{
    auto rewrites = resolveRewrites(store, dependencies);
    return rewriteStrings(toResolve, rewrites);
}

UnresolvedApp InstallableValue::toApp()
{
    auto cursor = getCursor();
    auto attrPath = cursor->getAttrPath(*state);

    auto type = cursor->getAttr(*state, "type")->getString(*state);

    std::string expected = !attrPath.empty() &&
        (attrPath[0] == "apps" || attrPath[0] == "defaultApp")
        ? "app" : "derivation";
    if (type != expected)
        throw Error("attribute '%s' should have type '%s'", cursor->getAttrPathStr(*state), expected);

    if (type == "app") {
        auto [program, context] = cursor->getAttr(*state, "program")->getStringWithContext(*state);

        std::vector<DerivedPath> context2;
        for (auto & c : context) {
            context2.emplace_back(std::visit(overloaded {
                [&](const NixStringContextElem::DrvDeep & d) -> DerivedPath {
                    /* We want all outputs of the drv */
                    return DerivedPath::Built {
                        .drvPath = makeConstantStorePathRef(d.drvPath),
                        .outputs = OutputsSpec::All {},
                    };
                },
                [&](const NixStringContextElem::Built & b) -> DerivedPath {
                    return DerivedPath::Built {
                        .drvPath = b.drvPath,
                        .outputs = OutputsSpec::Names { b.output },
                    };
                },
                [&](const NixStringContextElem::Opaque & o) -> DerivedPath {
                    return DerivedPath::Opaque {
                        .path = o.path,
                    };
                },
            }, c.raw));
        }

        return UnresolvedApp{App {
            .context = std::move(context2),
            .program = program,
        }};
    }

    else if (type == "derivation") {
        auto drvPath = cursor->forceDerivation(*state);
        auto outPath = cursor->getAttr(*state, "outPath")->getString(*state);
        auto outputName = cursor->getAttr(*state, "outputName")->getString(*state);
        auto name = cursor->getAttr(*state, "name")->getString(*state);
        auto aPname = cursor->maybeGetAttr(*state, "pname");
        auto aMeta = cursor->maybeGetAttr(*state, "meta");
        auto aMainProgram = aMeta ? aMeta->maybeGetAttr(*state, "mainProgram") : nullptr;
        auto mainProgram =
            aMainProgram
            ? aMainProgram->getString(*state)
            : aPname
            ? aPname->getString(*state)
            : DrvName(name).name;
        auto program = outPath + "/bin/" + mainProgram;
        return UnresolvedApp { App {
            .context = { DerivedPath::Built {
                .drvPath = makeConstantStorePathRef(drvPath),
                .outputs = OutputsSpec::Names { outputName },
            } },
            .program = program,
        }};
    }

    else
        throw Error("attribute '%s' has unsupported type '%s'", cursor->getAttrPathStr(*state), type);
}

// FIXME: move to libcmd
App UnresolvedApp::resolve(ref<Store> evalStore, ref<Store> store)
{
    auto res = unresolved;

    Installables installableContext;

    for (auto & ctxElt : unresolved.context)
        installableContext.push_back(
            make_ref<InstallableDerivedPath>(store, DerivedPath { ctxElt }));

    auto builtContext = Installable::build(evalStore, store, Realise::Outputs, installableContext);
    res.program = resolveString(*store, unresolved.program, builtContext);
    if (!store->isInStore(res.program))
        throw Error("app program '%s' is not in the Nix store", res.program);

    return res;
}

}
