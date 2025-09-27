#include "lix/libexpr/flake/flake.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/eval-settings.hh"
#include "lix/libexpr/extra-primops.hh"
#include "lix/libexpr/flake/lockfile.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libfetchers/fetchers.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/finally.hh"
#include "lix/libfetchers/fetch-settings.hh"
#include "lix/libutil/terminal.hh"

namespace nix {

using namespace flake;

namespace flake {

typedef std::pair<fetchers::Tree, FlakeRef> FetchedFlake;
typedef std::vector<std::pair<FlakeRef, FetchedFlake>> FlakeCache;

static std::optional<FetchedFlake> lookupInFlakeCache(
    const FlakeCache & flakeCache,
    const FlakeRef & flakeRef)
{
    // FIXME: inefficient.
    for (auto & i : flakeCache) {
        if (flakeRef == i.first) {
            debug("mapping '%s' to previously seen input '%s' -> '%s",
                flakeRef, i.first, i.second.second);
            return i.second;
        }
    }

    return std::nullopt;
}

static kj::Promise<Result<std::tuple<fetchers::Tree, FlakeRef, FlakeRef>>> fetchOrSubstituteTree(
    Evaluator & state,
    const FlakeRef & originalRef,
    bool allowLookup,
    FlakeCache & flakeCache)
try {
    auto fetched = lookupInFlakeCache(flakeCache, originalRef);
    FlakeRef resolvedRef = originalRef;

    if (!fetched) {
        if (originalRef.input.isDirect()) {
            fetched.emplace(TRY_AWAIT(originalRef.fetchTree(state.store)));
        } else {
            if (allowLookup) {
                resolvedRef = TRY_AWAIT(originalRef.resolve(state.store));
                auto fetchedResolved = lookupInFlakeCache(flakeCache, originalRef);
                if (!fetchedResolved) {
                    fetchedResolved.emplace(TRY_AWAIT(resolvedRef.fetchTree(state.store)));
                }
                flakeCache.push_back({resolvedRef, *fetchedResolved});
                fetched.emplace(*fetchedResolved);
            }
            else {
                throw Error("'%s' is an indirect flake reference, but registry lookups are not allowed", originalRef);
            }
        }
        flakeCache.push_back({originalRef, *fetched});
    }

    auto [tree, lockedRef] = *fetched;

    debug("got tree '%s' from '%s'",
        state.store->printStorePath(tree.storePath), lockedRef);

    state.paths.allowPath(tree.storePath);

    assert(!originalRef.input.getNarHash() || tree.storePath == originalRef.input.computeStorePath(*state.store));

    co_return {std::move(tree), resolvedRef, lockedRef};
} catch (...) {
    co_return result::current_exception();
}

static void forceTrivialValue(EvalState & state, Value & value, const PosIdx pos)
{
    if (value.isThunk() && value.isTrivial())
        state.forceValue(value, pos);
}


static void expectType(EvalState & state, ValueType type,
    Value & value, const PosIdx pos)
{
    forceTrivialValue(state, value, pos);
    if (value.type() != type)
        throw Error("expected %s but got %s at %s",
            showType(type), showType(value.type()), state.ctx.positions[pos]);
}

static std::pair<std::map<FlakeId, FlakeInput>, std::optional<fetchers::Attrs>> parseFlakeInputs(
    EvalState & state,
    Value & value,
    const PosIdx pos,
    const std::optional<Path> & baseDir,
    InputPath lockRootPath,
    unsigned depth,
    bool allowSelf
);

static void parseFlakeInputAttr(EvalState & state, const Attr & attr, fetchers::Attrs & attrs)
{
// Allow selecting a subset of enum values
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wswitch-enum"
    switch (attr.value.type()) {
    case nString:
        attrs.emplace(state.ctx.symbols[attr.name], std::string(attr.value.str()));
        break;
    case nBool:
        attrs.emplace(state.ctx.symbols[attr.name], Explicit<bool>{attr.value.boolean()});
        break;
    case nInt: {
        auto intValue = attr.value.integer().value;

        if (intValue < 0) {
            state.ctx.errors
                .make<EvalError>(
                    "negative value given for flake input attribute %1%: %2%",
                    state.ctx.symbols[attr.name],
                    intValue
                )
                .debugThrow();
        }
        uint64_t asUnsigned = intValue;
        attrs.emplace(state.ctx.symbols[attr.name], asUnsigned);
        break;
    }
    default:
        state.ctx.errors
            .make<TypeError>(
                "flake input attribute '%s' is %s while a string, Boolean, or integer is expected",
                state.ctx.symbols[attr.name],
                showType(attr.value)
            )
            .debugThrow();
    }
#pragma GCC diagnostic pop
}

static FlakeInput parseFlakeInput(
    EvalState & state,
    const std::string & inputName,
    Value & value,
    const PosIdx pos,
    const std::optional<Path> & baseDir,
    InputPath lockRootPath,
    unsigned depth
)
{
    expectType(state, nAttrs, value, pos);

    FlakeInput input;

    auto sInputs = state.ctx.symbols.create("inputs");
    auto sUrl = state.ctx.symbols.create("url");
    auto sFlake = state.ctx.symbols.create("flake");
    auto sFollows = state.ctx.symbols.create("follows");

    fetchers::Attrs attrs;
    std::optional<std::string> url;

    for (nix::Attr attr : *(value.attrs())) {
        try {
            if (attr.name == sUrl) {
                expectType(state, nString, attr.value, attr.pos);
                url = attr.value.str();
                attrs.emplace("url", *url);
            } else if (attr.name == sFlake) {
                expectType(state, nBool, attr.value, attr.pos);
                input.isFlake = attr.value.boolean();
            } else if (attr.name == sInputs) {
                input.overrides =
                    parseFlakeInputs(
                        state, attr.value, attr.pos, baseDir, lockRootPath, depth + 1, false
                    )
                        .first;
            } else if (attr.name == sFollows) {
                expectType(state, nString, attr.value, attr.pos);
                auto follows(parseInputPath(attr.value.str()));
                follows.insert(follows.begin(), lockRootPath.begin(), lockRootPath.end());
                input.follows = follows;
            } else {
                parseFlakeInputAttr(state, attr, attrs);
            }
        } catch (Error & e) {
            e.addTrace(
                state.ctx.positions[attr.pos],
                HintFmt("while evaluating flake attribute '%s'", state.ctx.symbols[attr.name]));
            throw;
        }
    }

    if (attrs.count("type"))
        try {
            input.ref = FlakeRef::fromAttrs(attrs);
        } catch (Error & e) {
            e.addTrace(state.ctx.positions[pos], HintFmt("while evaluating flake input"));
            throw;
        }
    else {
        attrs.erase("url");
        if (!attrs.empty())
            throw Error("unexpected flake input attribute '%s', at %s", attrs.begin()->first, state.ctx.positions[pos]);
        if (url)
            input.ref = parseFlakeRef(*url, baseDir, true, input.isFlake);
    }

    if (!input.follows && !input.ref && depth == 0)
        // in `input.nixops.inputs.nixpkgs.url = ...`, we assume `nixops` is from
        // the flake registry absent `ref`/`follows`, but we should not assume so
        // about `nixpkgs` (where `depth == 1`) as the `nixops` flake should
        // determine its default source
        input.ref = FlakeRef::fromAttrs({{"type", "indirect"}, {"id", inputName}});

    return input;
}

static std::pair<std::map<FlakeId, FlakeInput>, std::optional<fetchers::Attrs>> parseFlakeInputs(
    EvalState & state,
    Value & value,
    const PosIdx pos,
    const std::optional<Path> & baseDir,
    InputPath lockRootPath,
    unsigned depth,
    bool allowSelf = true
)
{
    std::map<FlakeId, FlakeInput> inputs;

    expectType(state, nAttrs, value, pos);

    std::optional<fetchers::Attrs> selfAttrs = std::nullopt;
    for (const nix::Attr & inputAttr : *value.attrs()) {
        std::string inputName{state.ctx.symbols[inputAttr.name]};
        if (inputName == "self") {
            experimentalFeatureSettings.require(Xp::FlakeSelfAttrs);

            if (!allowSelf) {
                throw Error(
                    "'self' input attributes not allowed at %s", state.ctx.positions[inputAttr.pos]
                );
            }
            expectType(state, nAttrs, inputAttr.value, inputAttr.pos);

            selfAttrs = selfAttrs.value_or(fetchers::Attrs{});
            for (auto & attr : *inputAttr.value.attrs()) {
                parseFlakeInputAttr(state, attr, *selfAttrs);
            }
        } else {
            inputs.emplace(
                inputName,
                parseFlakeInput(
                    state, inputName, inputAttr.value, inputAttr.pos, baseDir, lockRootPath, depth
                )
            );
        }
    }

    return {inputs, selfAttrs};
}

static std::optional<FlakeRef> applySelfAttrs(const FlakeRef & ref, const Flake & flake)
{
    // silently failing here is ok; since the parser requires the feature, we'll
    // crash much earlier if it wasn't enabled
    if (!flake.selfAttrs.has_value() || !experimentalFeatureSettings.isEnabled(Xp::FlakeSelfAttrs))
    {
        return std::nullopt;
    }

    static std::set<std::string> allowedAttrs{"submodules"};
    auto newRef(ref);

    for (auto & attr : *flake.selfAttrs) {
        if (!allowedAttrs.contains(attr.first)) {
            throw Error("flake 'self' attribute '%s' is not supported", attr.first);
        }
        newRef.input.attrs.insert_or_assign(attr.first, attr.second);
    }
    if (newRef != ref) {
        return newRef;
    }
    return std::nullopt;
}

static Flake getFlake(
    EvalState & state,
    const FlakeRef & originalRef,
    bool allowLookup,
    FlakeCache & flakeCache,
    InputPath lockRootPath)
{
    auto [sourceInfo, resolvedRef, lockedRef] =
        state.aio.blockOn(fetchOrSubstituteTree(state.ctx, originalRef, allowLookup, flakeCache));

    // We need to guard against symlink attacks, but before we start doing
    // filesystem operations we should make sure there's a flake.nix in the
    // first place.
    auto unsafeFlakeDir = sourceInfo.actualPath + "/" + lockedRef.subdir;
    auto unsafeFlakeFile = unsafeFlakeDir + "/flake.nix";
    if (!pathExists(unsafeFlakeFile))
        throw Error("source tree referenced by '%s' does not contain a '%s/flake.nix' file", lockedRef, lockedRef.subdir);

    // Guard against symlink attacks.
    auto flakeDir = canonPath(unsafeFlakeDir, true);
    auto flakeFile = canonPath(flakeDir + "/flake.nix", true);
    if (!isInDir(flakeFile, sourceInfo.actualPath))
        throw Error("'flake.nix' file of flake '%s' escapes from '%s'",
            lockedRef, state.ctx.store->printStorePath(sourceInfo.storePath));

    Flake flake {
        .originalRef = originalRef,
        .resolvedRef = resolvedRef,
        .lockedRef = lockedRef,
        .sourceInfo = std::make_shared<fetchers::Tree>(std::move(sourceInfo))
    };

    // FIXME: symlink attack
    auto resolvedFlakeFile = state.ctx.paths.resolveExprPath(CanonPath(flakeFile));
    Expr & flakeExpr = state.ctx.parseExprFromFile(resolvedFlakeFile);

    // Enforce that 'flake.nix' is a direct attrset, not a computation.
    if (!flakeExpr.try_cast<ExprAttrs>()) {
        state.ctx.errors.make<EvalError>("file '%s' must be an attribute set", resolvedFlakeFile).debugThrow();
    }

    Value vInfo;
    state.eval(flakeExpr, vInfo);

    if (auto description = vInfo.attrs()->get(state.ctx.s.description)) {
        expectType(state, nString, description->value, description->pos);
        flake.description = description->value.str();
    }

    auto sInputs = state.ctx.symbols.create("inputs");

    if (auto inputs = vInfo.attrs()->get(sInputs)) {
        auto [flakeInputs, selfAttrs] =
            parseFlakeInputs(state, inputs->value, inputs->pos, flakeDir, lockRootPath, 0, true);
        flake.inputs = std::move(flakeInputs);
        flake.selfAttrs = std::move(selfAttrs);
    }

    auto newLockedRef = applySelfAttrs(lockedRef, flake);
    if (newLockedRef.has_value()) {
        debug("refetching input '%s' due to self attribute", *newLockedRef);
        // FIXME: need to remove attrs that are invalidated by the changed input
        // attrs, such as 'narHash'.
        newLockedRef->input.attrs.erase("narHash");
        auto [sourceInfo2, resolvedRef2, lockedRef2] =
            state.aio.blockOn(fetchOrSubstituteTree(state.ctx, *newLockedRef, false, flakeCache));

        lockedRef = lockedRef2;
        flake.lockedRef = lockedRef;

        sourceInfo = sourceInfo2;
        flake.sourceInfo = std::make_shared<fetchers::Tree>(std::move(sourceInfo));

        resolvedRef = resolvedRef2;
        flake.resolvedRef = resolvedRef;
    }

    if (auto outputs = vInfo.attrs()->get(state.ctx.s.outputs)) {
        expectType(state, nFunction, outputs->value, outputs->pos);

        if (outputs->value.isLambda()) {
            if (auto pattern =
                    dynamic_cast<AttrsPattern *>(outputs->value.lambda().fun->pattern.get());
                pattern)
            {
                for (auto & formal : pattern->formals) {
                    if (formal.name != state.ctx.s.self)
                        flake.inputs.emplace(
                            state.ctx.symbols[formal.name],
                            FlakeInput{
                                .ref = parseFlakeRef(std::string(state.ctx.symbols[formal.name]))
                            }
                        );
                }
            }
        }

    } else
        throw Error("flake '%s' lacks attribute 'outputs'", lockedRef);

    auto sNixConfig = state.ctx.symbols.create("nixConfig");

    if (auto nixConfig = vInfo.attrs()->get(sNixConfig)) {
        expectType(state, nAttrs, nixConfig->value, nixConfig->pos);

        for (auto & setting : *nixConfig->value.attrs()) {
            forceTrivialValue(state, setting.value, setting.pos);
            if (setting.value.type() == nString) {
                flake.config.settings.emplace(
                    state.ctx.symbols[setting.name],
                    std::string(state.forceStringNoCtx(setting.value, setting.pos, ""))
                );
            } else if (setting.value.type() == nPath) {
                NixStringContext emptyContext = {};
                flake.config.settings.emplace(
                    state.ctx.symbols[setting.name],
                    state
                        .coerceToString(
                            setting.pos,
                            setting.value,
                            emptyContext,
                            "",
                            StringCoercionMode::Strict,
                            true,
                            true
                        )
                        .toOwned()
                );
            } else if (setting.value.type() == nInt) {
                flake.config.settings.emplace(
                    state.ctx.symbols[setting.name],
                    state.forceInt(setting.value, setting.pos, "").value
                );
            } else if (setting.value.type() == nBool) {
                flake.config.settings.emplace(
                    state.ctx.symbols[setting.name],
                    Explicit<bool>{state.forceBool(setting.value, setting.pos, "")}
                );
            } else if (setting.value.type() == nList) {
                std::vector<std::string> ss;
                for (auto & elem : setting.value.listItems()) {
                    if (elem.type() != nString) {
                        state.ctx.errors
                            .make<TypeError>(
                                "list element in flake configuration setting '%s' is %s while a "
                                "string is expected",
                                state.ctx.symbols[setting.name],
                                showType(setting.value)
                            )
                            .debugThrow();
                    }
                    ss.emplace_back(state.forceStringNoCtx(elem, setting.pos, ""));
                }
                flake.config.settings.emplace(state.ctx.symbols[setting.name], ss);
            } else {
                state.ctx.errors
                    .make<TypeError>(
                        "flake configuration setting '%s' is %s",
                        state.ctx.symbols[setting.name],
                        showType(setting.value)
                    )
                    .debugThrow();
            }
        }
    }

    for (auto & attr : *vInfo.attrs()) {
        if (attr.name != state.ctx.s.description &&
            attr.name != sInputs &&
            attr.name != state.ctx.s.outputs &&
            attr.name != sNixConfig)
            throw Error("flake '%s' has an unsupported attribute '%s', at %s",
                lockedRef, state.ctx.symbols[attr.name], state.ctx.positions[attr.pos]);
    }

    return flake;
}

Flake getFlake(EvalState & state, const FlakeRef & originalRef, bool allowLookup, FlakeCache & flakeCache)
{
    return getFlake(state, originalRef, allowLookup, flakeCache, {});
}

Flake getFlake(EvalState & state, const FlakeRef & originalRef, bool allowLookup)
{
    FlakeCache flakeCache;
    return getFlake(state, originalRef, allowLookup, flakeCache);
}

/* Recursively merge `overrides` into `overrideMap` */
static void updateOverrides(std::map<InputPath, FlakeInput> & overrideMap, const FlakeInputs & overrides,
                       const InputPath & inputPathPrefix)
{
    for (auto & [id, input] : overrides) {
        auto inputPath(inputPathPrefix);
        inputPath.push_back(id);

        /* Given
         *
         * { inputs.hydra.inputs.nix-eval-jobs.inputs.lix.follows = "lix"; }
         *
         * then `nix-eval-jobs` doesn't have an override.
         * It's neither replaced using follows nor by a different
         * URL. Thus no need to add it to overrides and thus re-fetch
         * it.
         */
        if (input.ref || input.follows) {
            // Do not override existing assignment from outer flake
            overrideMap.insert({inputPath, input});
        }

        updateOverrides(overrideMap, input.overrides, inputPath);
    }
}

/* Compute an in-memory lock file for the specified top-level flake,
   and optionally write it to file, if the flake is writable. */
LockedFlake lockFlake(
    EvalState & state,
    const FlakeRef & topRef,
    const LockFlags & lockFlags)
{
    experimentalFeatureSettings.require(Xp::Flakes);

    FlakeCache flakeCache;

    auto useRegistries = lockFlags.useRegistries.value_or(fetchSettings.useRegistries);

    auto flake = getFlake(state, topRef, useRegistries, flakeCache);

    if (lockFlags.applyNixConfig) {
        flake.config.apply();
        state.aio.blockOn(state.ctx.store->setOptions());
    }

    try {
        if (!fetchSettings.allowDirty && lockFlags.referenceLockFilePath) {
            throw Error("reference lock file was provided, but the `allow-dirty` setting is set to false");
        }

        // FIXME: symlink attack
        auto oldLockFile = LockFile::read(
            lockFlags.referenceLockFilePath.value_or(
                flake.sourceInfo->actualPath + "/" + flake.lockedRef.subdir + "/flake.lock"));

        debug("old lock file: %s", oldLockFile);

        std::map<InputPath, FlakeInput> overrides;
        std::set<InputPath> overridesUsed, updatesUsed;

        for (auto & i : lockFlags.inputOverrides)
            overrides.insert_or_assign(i.first, FlakeInput { .ref = i.second });

        LockFile newLockFile;

        std::vector<FlakeRef> parents;

        std::function<void(
            const FlakeInputs & flakeInputs,
            ref<Node> node,
            const InputPath & inputPathPrefix,
            std::shared_ptr<const Node> oldNode,
            const InputPath & lockRootPath,
            const Path & parentPath,
            bool trustLock)>
            computeLocks;

        computeLocks = [&](
            /* The inputs of this node, either from flake.nix or
               flake.lock. */
            const FlakeInputs & flakeInputs,
            /* The node whose locks are to be updated.*/
            ref<Node> node,
            /* The path to this node in the lock file graph. */
            const InputPath & inputPathPrefix,
            /* The old node, if any, from which locks can be
               copied. */
            std::shared_ptr<const Node> oldNode,
            const InputPath & lockRootPath,
            const Path & parentPath,
            bool trustLock)
        {
            debug("computing lock file node '%s'", printInputPath(inputPathPrefix));

            /* Get the overrides (i.e. attributes of the form
               'inputs.nixops.inputs.nixpkgs.url = ...'). */
            for (auto & [id, input] : flakeInputs) {
                auto inputPath(inputPathPrefix);
                inputPath.push_back(id);
                updateOverrides(overrides, input.overrides, inputPath);
            }

            /* Check whether this input has overrides for a
               non-existent input. */
            for (auto [inputPath, inputOverride] : overrides) {
                auto inputPath2(inputPath);
                auto follow = inputPath2.back();
                inputPath2.pop_back();
                if (inputPath2 == inputPathPrefix && !flakeInputs.count(follow))
                    printTaggedWarning(
                        "input '%s' has an override for a non-existent input '%s'",
                        printInputPath(inputPathPrefix),
                        follow
                    );
            }

            /* Go over the flake inputs, resolve/fetch them if
               necessary (i.e. if they're new or the flakeref changed
               from what's in the lock file). */
            for (auto & [id, input2] : flakeInputs) {
                auto inputPath(inputPathPrefix);
                inputPath.push_back(id);
                auto inputPathS = printInputPath(inputPath);
                debug("computing input '%s'", inputPathS);

                try {

                    /* Do we have an override for this input from one of the
                       ancestors? */
                    auto i = overrides.find(inputPath);
                    bool hasOverride = i != overrides.end();
                    if (hasOverride) {
                        overridesUsed.insert(inputPath);
                        // Respect the “flakeness” of the input even if we
                        // override it
                        i->second.isFlake = input2.isFlake;
                        if (!i->second.ref)
                            i->second.ref = input2.ref;
                        if (!i->second.follows)
                            i->second.follows = input2.follows;
                        // Note that `input.overrides` is not used in the following,
                        // so no need to merge it here (already done by `updateOverrides`)
                    }
                    auto & input = hasOverride ? i->second : input2;

                    /* Resolve 'follows' later (since it may refer to an input
                       path we haven't processed yet. */
                    if (input.follows) {
                        InputPath target;

                        target.insert(target.end(), input.follows->begin(), input.follows->end());

                        debug("input '%s' follows '%s'", inputPathS, printInputPath(target));
                        node->inputs.insert_or_assign(id, target);
                        continue;
                    }

                    assert(input.ref);

                    /* Do we have an entry in the existing lock file?
                       And the input is not in updateInputs? */
                    std::shared_ptr<LockedNode> oldLock;

                    updatesUsed.insert(inputPath);

                    if (oldNode && !lockFlags.inputUpdates.count(inputPath))
                        if (auto oldLock2 = get(oldNode->inputs, id))
                            if (auto oldLock3 = std::get_if<0>(&*oldLock2))
                                oldLock = *oldLock3;

                    if (oldLock
                        && oldLock->originalRef == *input.ref
                        && !hasOverride)
                    {
                        debug("keeping existing input '%s'", inputPathS);

                        /* Copy the input from the old lock since its flakeref
                           didn't change and there is no override from a
                           higher level flake. */
                        auto childNode = make_ref<LockedNode>(
                            oldLock->lockedRef, oldLock->originalRef, oldLock->isFlake);

                        node->inputs.insert_or_assign(id, childNode);

                        /* If we have this input in updateInputs, then we
                           must fetch the flake to update it. */
                        auto lb = lockFlags.inputUpdates.lower_bound(inputPath);

                        auto mustRefetch =
                            lb != lockFlags.inputUpdates.end()
                            && lb->size() > inputPath.size()
                            && std::equal(inputPath.begin(), inputPath.end(), lb->begin());

                        FlakeInputs fakeInputs;

                        if (!mustRefetch) {
                            /* No need to fetch this flake, we can be
                               lazy. However there may be new overrides on the
                               inputs of this flake, so we need to check
                               those. */
                            for (auto & i : oldLock->inputs) {
                                if (auto lockedNode = std::get_if<0>(&i.second)) {
                                    fakeInputs.emplace(i.first, FlakeInput {
                                        .ref = (*lockedNode)->originalRef,
                                        .isFlake = (*lockedNode)->isFlake,
                                    });
                                } else if (auto follows = std::get_if<1>(&i.second)) {
                                    if (!trustLock) {
                                        // It is possible that the flake has changed,
                                        // so we must confirm all the follows that are in the lock file are also in the flake.
                                        auto overridePath(inputPath);
                                        overridePath.push_back(i.first);
                                        auto o = overrides.find(overridePath);
                                        // If the override disappeared, we have to refetch the flake,
                                        // since some of the inputs may not be present in the lock file.
                                        if (o == overrides.end()) {
                                            mustRefetch = true;
                                            // There's no point populating the rest of the fake inputs,
                                            // since we'll refetch the flake anyways.
                                            break;
                                        }
                                    }
                                    auto absoluteFollows(lockRootPath);
                                    absoluteFollows.insert(absoluteFollows.end(), follows->begin(), follows->end());
                                    fakeInputs.emplace(i.first, FlakeInput {
                                        .follows = absoluteFollows,
                                    });
                                }
                            }
                        }

                        computeLocks(
                            mustRefetch
                            ? getFlake(state, oldLock->lockedRef, false, flakeCache, inputPath).inputs
                            : fakeInputs,
                            childNode, inputPath, oldLock, lockRootPath, parentPath, !mustRefetch);

                    } else {
                        /* We need to create a new lock file entry. So fetch
                           this input. */
                        debug("creating new input '%s'", inputPathS);

                        if (!lockFlags.allowUnlocked && !input.ref->input.isLocked())
                            throw Error("cannot update unlocked flake input '%s' in pure mode", inputPathS);

                        /* Note: in case of an --override-input, we use
                            the *original* ref (input2.ref) for the
                            "original" field, rather than the
                            override. This ensures that the override isn't
                            nuked the next time we update the lock
                            file. That is, overrides are sticky unless you
                            use --no-write-lock-file. */
                        auto ref = input2.ref ? *input2.ref : *input.ref;

                        if (input.isFlake) {
                            Path localPath = parentPath;
                            FlakeRef localRef = *input.ref;

                            // If this input is a path, recurse it down.
                            // This allows us to resolve path inputs relative to the current flake.
                            if (localRef.input.getType() == "path")
                                localPath = absPath(*input.ref->input.getSourcePath(), parentPath);

                            auto inputFlake = getFlake(state, localRef, useRegistries, flakeCache, inputPath);

                            auto childNode = make_ref<LockedNode>(inputFlake.lockedRef, ref);

                            node->inputs.insert_or_assign(id, childNode);

                            /* Guard against circular flake imports. */
                            for (auto & parent : parents)
                                if (parent == *input.ref)
                                    throw Error("found circular import of flake '%s'", parent);
                            parents.push_back(*input.ref);
                            Finally cleanup([&]() { parents.pop_back(); });

                            /* Recursively process the inputs of this
                               flake. Also, unless we already have this flake
                               in the top-level lock file, use this flake's
                               own lock file. */
                            computeLocks(
                                inputFlake.inputs, childNode, inputPath,
                                oldLock
                                ? std::dynamic_pointer_cast<const Node>(oldLock)
                                : LockFile::read(
                                    inputFlake.sourceInfo->actualPath + "/" + inputFlake.lockedRef.subdir + "/flake.lock").root.get_ptr(),
                                oldLock ? lockRootPath : inputPath,
                                localPath,
                                false);
                        }

                        else {
                            auto [sourceInfo, resolvedRef, lockedRef] =
                                state.aio.blockOn(fetchOrSubstituteTree(
                                    state.ctx, *input.ref, useRegistries, flakeCache
                                ));

                            auto childNode = make_ref<LockedNode>(lockedRef, ref, false);

                            node->inputs.insert_or_assign(id, childNode);
                        }
                    }

                } catch (Error & e) {
                    e.addTrace({}, "while updating the flake input '%s'", inputPathS);
                    throw;
                }
            }
        };

        // Bring in the current ref for relative path resolution if we have it
        auto parentPath = canonPath(flake.sourceInfo->actualPath + "/" + flake.lockedRef.subdir, true);

        computeLocks(
            flake.inputs,
            newLockFile.root,
            {},
            lockFlags.recreateLockFile ? nullptr : oldLockFile.root.get_ptr(),
            {},
            parentPath,
            false);

        for (auto & i : lockFlags.inputOverrides)
            if (!overridesUsed.count(i.first))
                printTaggedWarning(
                    "the flag '--override-input %s %s' does not match any input",
                    printInputPath(i.first),
                    i.second
                );

        for (auto & i : lockFlags.inputUpdates)
            if (!updatesUsed.count(i))
                printTaggedWarning(
                    "'%s' does not match any input of this flake", printInputPath(i)
                );

        /* Check 'follows' inputs. */
        newLockFile.check();

        debug("new lock file: %s", newLockFile);

        auto sourcePath = topRef.input.getSourcePath();

        /* Check whether we need to / can write the new lock file. */
        if (newLockFile != oldLockFile || lockFlags.outputLockFilePath) {

            auto diff = LockFile::diff(oldLockFile, newLockFile);

            if (lockFlags.writeLockFile) {
                if (sourcePath || lockFlags.outputLockFilePath) {
                    if (auto unlockedInput = newLockFile.isUnlocked()) {
                        if (fetchSettings.warnDirty)
                            printTaggedWarning(
                                "will not write lock file of flake '%s' because it has an unlocked "
                                "input ('%s')",
                                topRef,
                                *unlockedInput
                            );
                    } else {
                        if (!lockFlags.updateLockFile)
                            throw Error("flake '%s' requires lock file changes but they're not allowed due to '--no-update-lock-file'", topRef);

                        auto newLockFileS = fmt("%s\n", newLockFile);

                        if (lockFlags.outputLockFilePath) {
                            if (lockFlags.commitLockFile)
                                throw Error("'--commit-lock-file' and '--output-lock-file' are incompatible");
                            writeFile(*lockFlags.outputLockFilePath, newLockFileS);
                        } else {
                            auto relPath = (topRef.subdir == "" ? "" : topRef.subdir + "/") + "flake.lock";
                            auto outputLockFilePath = *sourcePath + "/" + relPath;

                            bool lockFileExists = pathExists(outputLockFilePath);

                            auto s = chomp(diff);
                            if (lockFileExists) {
                                if (s.empty())
                                    printTaggedWarning(
                                        "updating lock file '%s'", outputLockFilePath
                                    );
                                else
                                    printTaggedWarning(
                                        "updating lock file '%s':\n%s",
                                        outputLockFilePath,
                                        Uncolored(s)
                                    );
                            } else
                                printTaggedWarning(
                                    "creating lock file '%s':\n%s", outputLockFilePath, Uncolored(s)
                                );

                            std::optional<std::string> commitMessage = std::nullopt;

                            if (lockFlags.commitLockFile) {
                                std::string cm;

                                cm = fetchSettings.commitLockFileSummary.get();

                                if (cm == "") {
                                    cm = fmt("%s: %s", relPath, lockFileExists ? "Update" : "Add");
                                }

                                cm += "\n\nFlake lock file updates:\n\n";
                                cm += filterANSIEscapes(diff, true);
                                commitMessage = cm;
                            }

                            state.aio.blockOn(topRef.input.putFile(
                                CanonPath(
                                    (topRef.subdir == "" ? "" : topRef.subdir + "/") + "flake.lock"
                                ),
                                newLockFileS,
                                commitMessage
                            ));
                        }

                        /* Rewriting the lockfile changed the top-level
                           repo, so we should re-read it. FIXME: we could
                           also just clear the 'rev' field... */
                        auto prevLockedRef = flake.lockedRef;
                        FlakeCache dummyCache;
                        flake = getFlake(state, topRef, useRegistries, dummyCache);

                        if (lockFlags.commitLockFile &&
                            flake.lockedRef.input.getRev() &&
                            prevLockedRef.input.getRev() != flake.lockedRef.input.getRev())
                            printTaggedWarning(
                                "committed new revision '%s'",
                                flake.lockedRef.input.getRev()->gitRev()
                            );

                        /* Make sure that we picked up the change,
                           i.e. the tree should usually be dirty
                           now. Corner case: we could have reverted from a
                           dirty to a clean tree! */
                        if (flake.lockedRef.input == prevLockedRef.input
                            && !flake.lockedRef.input.isLocked())
                            throw Error("'%s' did not change after I updated its 'flake.lock' file; is 'flake.lock' under version control?", flake.originalRef);
                    }
                } else
                    throw Error("cannot write modified lock file of flake '%s' (use '--no-write-lock-file' to ignore)", topRef);
            } else {
                printTaggedWarning(
                    "not writing modified lock file of flake '%s':\n%s", topRef, chomp(diff)
                );
                flake.forceDirty = true;
            }
        }

        return LockedFlake { .flake = std::move(flake), .lockFile = std::move(newLockFile) };

    } catch (Error & e) {
        e.addTrace({}, "while updating the lock file of flake '%s'", flake.lockedRef.to_string());
        throw;
    }
}

void callFlake(EvalState & state,
    const LockedFlake & lockedFlake,
    Value & vRes)
{
    Value vLocks;
    Value vRootSrc;
    Value vRootSubdir;
    Value vTmp1;
    Value vTmp2;

    vLocks.mkString(lockedFlake.lockFile.to_string());

    emitTreeAttrs(
        state.ctx,
        *lockedFlake.flake.sourceInfo,
        lockedFlake.flake.lockedRef.input,
        vRootSrc,
        false,
        lockedFlake.flake.forceDirty
    );

    vRootSubdir.mkString(lockedFlake.flake.lockedRef.subdir);

    if (!state.ctx.caches.vCallFlake) {
        state.ctx.caches.vCallFlake = allocRootValue({});
        state.eval(
            state.ctx.parseExprFromString(
#include "call-flake.nix.gen.hh"
                , CanonPath::root
            ),
            *state.ctx.caches.vCallFlake
        );
    }

    state.callFunction(*state.ctx.caches.vCallFlake, vLocks, vTmp1, noPos);
    state.callFunction(vTmp1, vRootSrc, vTmp2, noPos);
    state.callFunction(vTmp2, vRootSubdir, vRes, noPos);
}

void prim_getFlake(EvalState & state, Value * * args, Value & v)
{
    std::string flakeRefS(state.forceStringNoCtx(*args[0], noPos, "while evaluating the argument passed to builtins.getFlake"));
    auto flakeRef = parseFlakeRef(flakeRefS, {}, true);
    if (evalSettings.pureEval && !flakeRef.input.isLocked())
        throw Error("cannot call 'getFlake' on unlocked flake reference '%s' (use --impure to override)", flakeRefS);

    callFlake(state,
        lockFlake(state, flakeRef,
            LockFlags {
                .updateLockFile = false,
                .writeLockFile = false,
                .useRegistries = !evalSettings.pureEval && fetchSettings.useRegistries,
                .allowUnlocked = !evalSettings.pureEval,
            }),
        v);
}

void prim_parseFlakeRef(
    EvalState & state,
    Value * * args,
    Value & v)
{
    std::string flakeRefS(state.forceStringNoCtx(*args[0], noPos,
        "while evaluating the argument passed to builtins.parseFlakeRef"));
    auto attrs = parseFlakeRef(flakeRefS, {}, true).toAttrs();
    auto binds = state.ctx.buildBindings(attrs.size());
    for (const auto & [key, value] : attrs) {
        auto s = state.ctx.symbols.create(key);
        auto & vv = binds.alloc(s);
        std::visit(overloaded {
            [&vv](const std::string    & value) { vv.mkString(value); },
            [&vv](const uint64_t       & value) { vv.mkInt(value);    },
            [&vv](const Explicit<bool> & value) { vv.mkBool(value.t); }
        }, value);
    }
    v.mkAttrs(binds);
}

void prim_flakeRefToString(
    EvalState & state,
    Value * * args,
    Value & v)
{
    state.forceAttrs(*args[0], noPos,
        "while evaluating the argument passed to builtins.flakeRefToString");
    fetchers::Attrs attrs;
    for (const auto & attr : *args[0]->attrs()) {
        auto t = attr.value.type();
        if (t == nInt) {
            auto intValue = attr.value.integer().value;

            if (intValue < 0) {
                state.ctx.errors.make<EvalError>("negative value given for flake ref attr %1%: %2%", state.ctx.symbols[attr.name], intValue).debugThrow();
            }
            uint64_t asUnsigned = intValue;

            attrs.emplace(state.ctx.symbols[attr.name], asUnsigned);
        } else if (t == nBool) {
            attrs.emplace(state.ctx.symbols[attr.name], Explicit<bool>{attr.value.boolean()});
        } else if (t == nString) {
            attrs.emplace(state.ctx.symbols[attr.name], std::string(attr.value.str()));
        } else {
            state.ctx.errors
                .make<EvalError>(
                    "flake reference attribute sets may only contain integers, Booleans, "
                    "and strings, but attribute '%s' is %s",
                    state.ctx.symbols[attr.name],
                    showType(attr.value)
                )
                .debugThrow();
        }
    }
    auto flakeRef = FlakeRef::fromAttrs(attrs);
    v.mkString(flakeRef.to_string());
}

}

Fingerprint LockedFlake::getFingerprint() const
{
    // FIXME: as an optimization, if the flake contains a lock file
    // and we haven't changed it, then it's sufficient to use
    // flake.sourceInfo.storePath for the fingerprint.
    return hashString(HashType::SHA256,
        fmt("%s;%s;%d;%d;%s",
            flake.sourceInfo->storePath.to_string(),
            flake.lockedRef.subdir,
            flake.lockedRef.input.getRevCount().value_or(0),
            flake.lockedRef.input.getLastModified().value_or(0),
            lockFile));
}

Flake::~Flake() { }

}
