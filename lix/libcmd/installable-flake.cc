#include "lix/libcmd/installable-flake.hh"
#include "lix/libstore/outputs-spec.hh"
#include "lix/libcmd/command.hh"
#include "lix/libexpr/attr-path.hh"
#include "lix/libcmd/common-eval-args.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/flake/flake.hh"
#include "lix/libexpr/eval-cache.hh"

#include <nlohmann/json.hpp>

namespace nix {

std::vector<std::string> InstallableFlake::getActualAttrPaths()
{
    std::vector<std::string> res;
    if (attrPaths.size() == 1 && attrPaths.front().starts_with(".")){
        res.push_back(attrPaths.front().substr(1));
        return res;
    }

    for (auto & prefix : prefixes)
        res.push_back(prefix + *attrPaths.begin());

    for (auto & s : attrPaths)
        res.push_back(s);

    return res;
}

static std::string showAttrPaths(const std::vector<std::string> & paths)
{
    std::string s;
    for (const auto & [n, i] : enumerate(paths)) {
        if (n > 0) s += n + 1 == paths.size() ? " or " : ", ";
        s += '\''; s += i; s += '\'';
    }
    return s;
}

InstallableFlake::InstallableFlake(
    SourceExprCommand * cmd,
    ref<eval_cache::CachingEvaluator> state,
    FlakeRef && flakeRef,
    std::string_view fragment,
    ExtendedOutputsSpec extendedOutputsSpec,
    Strings attrPaths,
    Strings prefixes,
    const flake::LockFlags & lockFlags)
    : InstallableValue(state),
      flakeRef(flakeRef),
      attrPaths(fragment == "" ? attrPaths : Strings{(std::string) fragment}),
      prefixes(fragment == "" ? Strings{} : prefixes),
      extendedOutputsSpec(std::move(extendedOutputsSpec)),
      lockFlags(lockFlags)
{
    if (cmd && cmd->getAutoArgs(*state)->size())
        throw UsageError("'--arg' and '--argstr' are incompatible with flakes");
}

DerivedPathsWithInfo InstallableFlake::toDerivedPaths(EvalState & state)
{
    Activity act(*logger, lvlTalkative, actUnknown, fmt("evaluating derivation '%s'", what()));

    auto attr = getCursor(state);

    auto attrPath = attr->getAttrPathStr(state);

    if (!attr->isDerivation(state)) {

        // FIXME: use eval cache?
        auto v = attr->forceValue(state);

        if (std::optional derivedPathWithInfo = trySinglePathToDerivedPaths(
                state, v, noPos, fmt("while evaluating the flake output attribute '%s'", attrPath)
            ))
        {
            return { *derivedPathWithInfo };
        } else {
            throw Error(
                "expected flake output attribute '%s' to be a derivation or path but found %s: %s",
                attrPath,
                showType(v),
                ValuePrinter(state, v, errorPrintOptions)
            );
        }
    }

    auto drvPath = attr->forceDerivation(state);

    std::optional<NixInt::Inner> priority;

    if (attr->maybeGetAttr(state, "outputSpecified")) {
    } else if (auto aMeta = attr->maybeGetAttr(state, "meta")) {
        if (auto aPriority = aMeta->maybeGetAttr(state, "priority"))
            priority = aPriority->getInt(state).value;
    }

    return {{
        .path = DerivedPath::Built {
            .drvPath = makeConstantStorePath(std::move(drvPath)),
            .outputs = std::visit(overloaded {
                [&](const ExtendedOutputsSpec::Default & d) -> OutputsSpec {
                    std::set<std::string> outputsToInstall;
                    if (auto aOutputSpecified = attr->maybeGetAttr(state, "outputSpecified")) {
                        if (aOutputSpecified->getBool(state)) {
                            if (auto aOutputName = attr->maybeGetAttr(state, "outputName"))
                                outputsToInstall = { aOutputName->getString(state) };
                        }
                    } else if (auto aMeta = attr->maybeGetAttr(state, "meta")) {
                        if (auto aOutputsToInstall = aMeta->maybeGetAttr(state, "outputsToInstall"))
                            for (auto & s : aOutputsToInstall->getListOfStrings(state))
                                outputsToInstall.insert(s);
                    }

                    if (outputsToInstall.empty())
                        outputsToInstall.insert("out");

                    return OutputsSpec::Names { std::move(outputsToInstall) };
                },
                [&](const ExtendedOutputsSpec::Explicit & e) -> OutputsSpec {
                    return e;
                },
            }, extendedOutputsSpec.raw),
        },
        .info = make_ref<ExtraPathInfoFlake>(
            ExtraPathInfoValue::Value {
                .priority = priority,
                .attrPath = attrPath,
                .extendedOutputsSpec = extendedOutputsSpec,
            },
            ExtraPathInfoFlake::Flake {
                .originalRef = flakeRef,
                .lockedRef = getLockedFlake(state)->flake.lockedRef,
            }),
    }};
}

std::pair<Value, PosIdx> InstallableFlake::toValue(EvalState & state)
{
    return {getCursor(state)->forceValue(state), noPos};
}

std::vector<ref<eval_cache::AttrCursor>>
InstallableFlake::getCursors(EvalState & state)
{
    auto evalCache = openEvalCache(*evaluator, getLockedFlake(state));

    auto root = evalCache->getRoot();

    std::vector<ref<eval_cache::AttrCursor>> res;

    Suggestions suggestions;
    auto attrPaths = getActualAttrPaths();

    for (auto & attrPath : attrPaths) {
        debug("trying flake output attribute '%s'", attrPath);

        auto attr = root->findAlongAttrPath(state, parseAttrPath(attrPath));
        if (attr) {
            res.push_back(ref(*attr));
        } else {
            suggestions += attr.getSuggestions();
        }
    }

    if (res.size() == 0)
        throw Error(
            suggestions,
            "flake '%s' does not provide attribute %s",
            flakeRef,
            showAttrPaths(attrPaths));

    return res;
}

std::shared_ptr<flake::LockedFlake> InstallableFlake::getLockedFlake(EvalState & state) const
{
    if (!_lockedFlake) {
        flake::LockFlags lockFlagsApplyConfig = lockFlags;
        // FIXME why this side effect?
        lockFlagsApplyConfig.applyNixConfig = true;
        _lockedFlake = std::make_shared<flake::LockedFlake>(lockFlake(state, flakeRef, lockFlagsApplyConfig));
    }
    return _lockedFlake;
}

FlakeRef InstallableFlake::nixpkgsFlakeRef(EvalState & state) const
{
    auto lockedFlake = getLockedFlake(state);

    if (auto nixpkgsInput = lockedFlake->lockFile.findInput({"nixpkgs"})) {
        if (auto lockedNode = std::dynamic_pointer_cast<const flake::LockedNode>(nixpkgsInput)) {
            debug("using nixpkgs flake '%s'", lockedNode->lockedRef);
            return std::move(lockedNode->lockedRef);
        }
    }

    return defaultNixpkgsFlakeRef();
}

}
