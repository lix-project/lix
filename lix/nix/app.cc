#include "lix/libcmd/installables.hh"
#include "lix/libcmd/installable-derived-path.hh"
#include "lix/libcmd/installable-value.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libexpr/eval-inline.hh"
#include "lix/libexpr/eval-cache.hh"
#include "lix/libstore/names.hh"
#include "lix/libcmd/command.hh"
#include "lix/libstore/derivations.hh"

namespace nix {

UnresolvedApp InstallableValue::toApp(EvalState & state)
{
    auto cursor = getCursor(state);
    auto attrPath = cursor->getAttrPath(state);

    auto type = cursor->getAttr(state, "type")->getString(state);

    std::string expected = !attrPath.empty() &&
        (attrPath[0] == "apps" || attrPath[0] == "defaultApp")
        ? "app" : "derivation";
    if (type != expected)
        throw Error("attribute '%s' should have type '%s'", cursor->getAttrPathStr(state), expected);

    if (type == "app") {
        auto [program, context] = cursor->getAttr(state, "program")->getStringWithContext(state);

        std::vector<DerivedPath> context2;
        for (auto & c : context) {
            context2.emplace_back(std::visit(overloaded {
                [&](const NixStringContextElem::DrvDeep & d) -> DerivedPath {
                    /* We want all outputs of the drv */
                    return DerivedPath::Built {
                        .drvPath = makeConstantStorePath(d.drvPath),
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
        auto drvPath = cursor->forceDerivation(state);
        auto outPath = cursor->getAttr(state, "outPath")->getString(state);
        auto outputName = cursor->getAttr(state, "outputName")->getString(state);
        auto name = cursor->getAttr(state, "name")->getString(state);
        auto aPname = cursor->maybeGetAttr(state, "pname");
        auto aMeta = cursor->maybeGetAttr(state, "meta");
        auto aMainProgram = aMeta ? aMeta->maybeGetAttr(state, "mainProgram") : nullptr;
        auto mainProgram =
            aMainProgram
            ? aMainProgram->getString(state)
            : aPname
            ? aPname->getString(state)
            : DrvName(name).name;
        auto program = outPath + "/bin/" + mainProgram;
        return UnresolvedApp { App {
            .context = { DerivedPath::Built {
                .drvPath = makeConstantStorePath(drvPath),
                .outputs = OutputsSpec::Names { outputName },
            } },
            .program = program,
        }};
    }

    else
        throw Error("attribute '%s' has unsupported type '%s'", cursor->getAttrPathStr(state), type);
}

// FIXME: move to libcmd
App UnresolvedApp::resolve(EvalState & state, ref<Store> evalStore, ref<Store> store)
{
    auto res = unresolved;

    Installables installableContext;

    for (auto & ctxElt : unresolved.context)
        installableContext.push_back(
            make_ref<InstallableDerivedPath>(store, DerivedPath { ctxElt }));

    Installable::build(state, evalStore, store, Realise::Outputs, installableContext);
    if (!store->isInStore(res.program))
        throw Error("app program '%s' is not in the Nix store", res.program);

    return res;
}

}
