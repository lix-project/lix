#pragma once
///@file

#include "lix/libcmd/installable-value.hh"
#include "lix/libexpr/eval-cache.hh"
#include "lix/libstore/outputs-spec.hh"
#include "lix/libcmd/command.hh"
#include "lix/libcmd/common-eval-args.hh"
#include "lix/libexpr/eval.hh"

namespace nix {

class InstallableAttrPath : public InstallableValue
{
    SourceExprCommand & cmd;
    RootValue v;
    std::string attrPath;
    ExtendedOutputsSpec extendedOutputsSpec;

    InstallableAttrPath(
        ref<eval_cache::CachingEvaluator> state,
        SourceExprCommand & cmd,
        Value & v,
        const std::string & attrPath,
        ExtendedOutputsSpec extendedOutputsSpec
    );

    std::string what() const override { return attrPath; };

    std::pair<Value, PosIdx> toValue(EvalState & state) override;

    DerivedPathsWithInfo toDerivedPaths(EvalState & state) override;

public:

    static InstallableAttrPath parse(
        ref<eval_cache::CachingEvaluator> state,
        SourceExprCommand & cmd,
        Value & v,
        std::string_view prefix,
        ExtendedOutputsSpec extendedOutputsSpec
    );
};

}
