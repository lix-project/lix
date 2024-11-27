#include "lix/libstore/globals.hh"
#include "lix/libcmd/installable-flake.hh"
#include "lix/libcmd/installable-derived-path.hh"
#include "lix/libstore/outputs-spec.hh"
#include "lix/libcmd/command.hh"
#include "lix/libexpr/attr-path.hh"
#include "lix/libcmd/common-eval-args.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libexpr/eval-inline.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/get-drvs.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libmain/shared.hh"
#include "lix/libexpr/flake/flake.hh"
#include "lix/libexpr/eval-cache.hh"
#include "lix/libutil/url.hh"
#include "lix/libfetchers/registry.hh"
#include "lix/libstore/build-result.hh"

#include <regex>
#include <queue>

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

Value * InstallableFlake::getFlakeOutputs(const flake::LockedFlake & lockedFlake)
{
    auto vFlake = state->mem.allocValue();

    callFlake(*state, lockedFlake, *vFlake);

    auto aOutputs = vFlake->attrs->get(state->symbols.create("outputs"));
    assert(aOutputs);

    state->forceValue(*aOutputs->value, aOutputs->value->determinePos(noPos));

    return aOutputs->value;
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
    ref<eval_cache::CachingEvalState> state,
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

DerivedPathsWithInfo InstallableFlake::toDerivedPaths()
{
    Activity act(*logger, lvlTalkative, actUnknown, fmt("evaluating derivation '%s'", what()));

    auto attr = getCursor();

    auto attrPath = attr->getAttrPathStr();

    if (!attr->isDerivation()) {

        // FIXME: use eval cache?
        auto v = attr->forceValue();

        if (std::optional derivedPathWithInfo = trySinglePathToDerivedPaths(
            v,
            noPos,
            fmt("while evaluating the flake output attribute '%s'", attrPath)))
        {
            return { *derivedPathWithInfo };
        } else {
            throw Error(
                "expected flake output attribute '%s' to be a derivation or path but found %s: %s",
                attrPath,
                showType(v),
                ValuePrinter(*this->state, v, errorPrintOptions)
            );
        }
    }

    auto drvPath = attr->forceDerivation();

    std::optional<NixInt::Inner> priority;

    if (attr->maybeGetAttr(state->s.outputSpecified)) {
    } else if (auto aMeta = attr->maybeGetAttr(state->s.meta)) {
        if (auto aPriority = aMeta->maybeGetAttr("priority"))
            priority = aPriority->getInt().value;
    }

    return {{
        .path = DerivedPath::Built {
            .drvPath = makeConstantStorePathRef(std::move(drvPath)),
            .outputs = std::visit(overloaded {
                [&](const ExtendedOutputsSpec::Default & d) -> OutputsSpec {
                    std::set<std::string> outputsToInstall;
                    if (auto aOutputSpecified = attr->maybeGetAttr(state->s.outputSpecified)) {
                        if (aOutputSpecified->getBool()) {
                            if (auto aOutputName = attr->maybeGetAttr("outputName"))
                                outputsToInstall = { aOutputName->getString() };
                        }
                    } else if (auto aMeta = attr->maybeGetAttr(state->s.meta)) {
                        if (auto aOutputsToInstall = aMeta->maybeGetAttr("outputsToInstall"))
                            for (auto & s : aOutputsToInstall->getListOfStrings())
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
                .lockedRef = getLockedFlake()->flake.lockedRef,
            }),
    }};
}

std::pair<Value *, PosIdx> InstallableFlake::toValue()
{
    return {&getCursor()->forceValue(), noPos};
}

std::vector<ref<eval_cache::AttrCursor>>
InstallableFlake::getCursors()
{
    auto evalCache = openEvalCache(*state, getLockedFlake());

    auto root = evalCache->getRoot();

    std::vector<ref<eval_cache::AttrCursor>> res;

    Suggestions suggestions;
    auto attrPaths = getActualAttrPaths();

    for (auto & attrPath : attrPaths) {
        debug("trying flake output attribute '%s'", attrPath);

        auto attr = root->findAlongAttrPath(parseAttrPath(*state, attrPath));
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

std::shared_ptr<flake::LockedFlake> InstallableFlake::getLockedFlake() const
{
    if (!_lockedFlake) {
        flake::LockFlags lockFlagsApplyConfig = lockFlags;
        // FIXME why this side effect?
        lockFlagsApplyConfig.applyNixConfig = true;
        _lockedFlake = std::make_shared<flake::LockedFlake>(lockFlake(*state, flakeRef, lockFlagsApplyConfig));
    }
    return _lockedFlake;
}

FlakeRef InstallableFlake::nixpkgsFlakeRef() const
{
    auto lockedFlake = getLockedFlake();

    if (auto nixpkgsInput = lockedFlake->lockFile.findInput({"nixpkgs"})) {
        if (auto lockedNode = std::dynamic_pointer_cast<const flake::LockedNode>(nixpkgsInput)) {
            debug("using nixpkgs flake '%s'", lockedNode->lockedRef);
            return std::move(lockedNode->lockedRef);
        }
    }

    return defaultNixpkgsFlakeRef();
}

}
