#include "lix/libcmd/installable-attr-path.hh"
#include "lix/libstore/outputs-spec.hh"
#include "lix/libcmd/command.hh"
#include "lix/libexpr/attr-path.hh"
#include "lix/libcmd/common-eval-args.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/get-drvs.hh"
#include "lix/libexpr/flake/flake.hh"

namespace nix {

InstallableAttrPath::InstallableAttrPath(
    ref<eval_cache::CachingEvaluator> state,
    SourceExprCommand & cmd,
    Value & v,
    const std::string & attrPath,
    ExtendedOutputsSpec extendedOutputsSpec
)
    : InstallableValue(state)
    , cmd(cmd)
    , v(allocRootValue(v))
    , attrPath(attrPath)
    , extendedOutputsSpec(std::move(extendedOutputsSpec))
{ }

std::pair<Value, PosIdx> InstallableAttrPath::toValue(EvalState & state)
{
    auto [vRes, pos] = findAlongAttrPath(state, attrPath, *cmd.getAutoArgs(*evaluator), *v);
    state.forceValue(vRes, pos);
    return {vRes, pos};
}

DerivedPathsWithInfo InstallableAttrPath::toDerivedPaths(EvalState & state)
{
    auto [v, pos] = toValue(state);

    if (std::optional derivedPathWithInfo = trySinglePathToDerivedPaths(
            state, v, pos, fmt("while evaluating the attribute '%s'", attrPath)
        ))
    {
        return { *derivedPathWithInfo };
    }

    Bindings & autoArgs = *cmd.getAutoArgs(*evaluator);

    DrvInfos drvInfos;
    getDerivations(state, v, "", autoArgs, drvInfos, false);

    // Backward compatibility hack: group results by drvPath. This
    // helps keep .all output together.
    std::map<StorePath, OutputsSpec> byDrvPath;

    for (auto & drvInfo : drvInfos) {
        auto drvPath = drvInfo.queryDrvPath(state);
        if (!drvPath)
            throw Error("'%s' is not a derivation", what());

        auto newOutputs = std::visit(overloaded {
            [&](const ExtendedOutputsSpec::Default & d) -> OutputsSpec {
                std::set<std::string> outputsToInstall;
                for (auto & output : drvInfo.queryOutputs(state, false, true))
                    outputsToInstall.insert(output.first);
                return OutputsSpec::Names { std::move(outputsToInstall) };
            },
            [&](const ExtendedOutputsSpec::Explicit & e) -> OutputsSpec {
                return e;
            },
        }, extendedOutputsSpec.raw);

        auto [iter, didInsert] = byDrvPath.emplace(*drvPath, newOutputs);

        if (!didInsert)
            iter->second = iter->second.union_(newOutputs);
    }

    DerivedPathsWithInfo res;
    for (auto & [drvPath, outputs] : byDrvPath)
        res.push_back({
            .path = DerivedPath::Built {
                .drvPath = makeConstantStorePath(drvPath),
                .outputs = outputs,
            },
            .info = make_ref<ExtraPathInfoValue>(ExtraPathInfoValue::Value {
                .extendedOutputsSpec = outputs,
                /* FIXME: reconsider backwards compatibility above
                   so we can fill in this info. */
            }),
        });

    return res;
}

InstallableAttrPath InstallableAttrPath::parse(
    ref<eval_cache::CachingEvaluator> state,
    SourceExprCommand & cmd,
    Value & v,
    std::string_view prefix,
    ExtendedOutputsSpec extendedOutputsSpec
)
{
    return {
        state, cmd, v,
        prefix == "." ? "" : std::string { prefix },
        std::move(extendedOutputsSpec),
    };
}

}
