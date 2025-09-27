#include "lix/libstore/globals.hh"
#include "lix/libcmd/installables.hh"
#include "lix/libcmd/installable-derived-path.hh"
#include "lix/libcmd/installable-attr-path.hh"
#include "lix/libcmd/installable-flake.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/logging.hh"
#include "lix/libstore/outputs-spec.hh"
#include "lix/libcmd/command.hh"
#include "lix/libexpr/attr-path.hh"
#include "lix/libcmd/common-eval-args.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/eval-settings.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libmain/shared.hh"
#include "lix/libexpr/flake/flake.hh"
#include "lix/libexpr/eval-cache.hh"
#include "lix/libfetchers/registry.hh"
#include "lix/libstore/build-result.hh"


namespace nix {

void completeFlakeInputPath(
    AddCompletions & completions,
    EvalState & evalState,
    const std::vector<FlakeRef> & flakeRefs,
    std::string_view prefix)
{
    for (auto & flakeRef : flakeRefs) {
        auto flake = flake::getFlake(evalState, flakeRef, true);
        for (auto & input : flake.inputs)
            if (input.first.starts_with(prefix))
                completions.add(input.first);
    }
}

MixFlakeOptions::MixFlakeOptions()
{
    auto category = "Common flake-related options";

    addFlag({
        .longName = "no-update-lock-file",
        .description = "Do not allow any updates to the flake's lock file.",
        .category = category,
        .handler = {&lockFlags.updateLockFile, false}
    });

    addFlag({
        .longName = "no-write-lock-file",
        .description = "Do not write the flake's newly generated lock file.",
        .category = category,
        .handler = {&lockFlags.writeLockFile, false}
    });

    addFlag({
        .longName = "no-registries",
        .description =
          "Don't allow lookups in the flake registries. This option is deprecated; use `--no-use-registries`.",
        .category = category,
        .handler = {[&]() {
            lockFlags.useRegistries = false;
            printTaggedWarning("'--no-registries' is deprecated; use '--no-use-registries'");
        }}
    });

    addFlag({
        .longName = "commit-lock-file",
        .description = "Commit changes to the flake's lock file.",
        .category = category,
        .handler = {&lockFlags.commitLockFile, true}
    });

    addFlag({
        .longName = "override-input",
        .description = "Override a specific flake input (e.g. `dwarffs/nixpkgs`). This implies `--no-write-lock-file`.",
        .category = category,
        .labels = {"input-path", "flake-url"},
        .handler = {[&](std::string inputPath, std::string flakeRef) {
            lockFlags.writeLockFile = false;
            lockFlags.inputOverrides.insert_or_assign(
                flake::parseInputPath(inputPath),
                parseFlakeRef(flakeRef, absPath("."), true));
        }},
        .completer = {[&](AddCompletions & completions, size_t n, std::string_view prefix) {
            if (n == 0) {
                completeFlakeInputPath(
                    completions, *getEvaluator()->begin(aio()), getFlakeRefsForCompletion(), prefix
                );
            } else if (n == 1) {
                completeFlakeRef(aio(), completions, getEvaluator()->store, prefix);
            }
        }}
    });

    addFlag({
        .longName = "reference-lock-file",
        .description = "Read the given lock file instead of `flake.lock` within the top-level flake.",
        .category = category,
        .labels = {"flake-lock-path"},
        .handler = {[&](std::string lockFilePath) {
            lockFlags.referenceLockFilePath = lockFilePath;
        }},
        .completer = completePath
    });

    addFlag({
        .longName = "output-lock-file",
        .description = "Write the given lock file instead of `flake.lock` within the top-level flake.",
        .category = category,
        .labels = {"flake-lock-path"},
        .handler = {[&](std::string lockFilePath) {
            lockFlags.outputLockFilePath = lockFilePath;
        }},
        .completer = completePath
    });

    addFlag({
        .longName = "inputs-from",
        .description = "Use the inputs of the specified flake as registry entries.",
        .category = category,
        .labels = {"flake-url"},
        .handler = {[&](std::string flakeRef) {
            auto evalState = getEvaluator()->begin(aio());
            auto flake = flake::lockFlake(
                *evalState,
                parseFlakeRef(flakeRef, absPath(".")),
                { .writeLockFile = false });
            for (auto & [inputName, input] : flake.lockFile.root->inputs) {
                auto input2 = flake.lockFile.findInput({inputName}); // resolve 'follows' nodes
                if (auto input3 = std::dynamic_pointer_cast<const flake::LockedNode>(input2)) {
                    overrideRegistry(
                        fetchers::Input::fromAttrs({{"type","indirect"}, {"id", inputName}}),
                        input3->lockedRef.input,
                        {});
                }
            }
        }},
        .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
            completeFlakeRef(aio(), completions, getEvaluator()->store, prefix);
        }}
    });
}

SourceExprCommand::SourceExprCommand()
{
    addFlag({
        .longName = "file",
        .shortName = 'f',
        .description =
            "Interpret [*installables*](@docroot@/command-ref/new-cli/nix.md#installables) as attribute paths relative to the Nix expression stored in *file*. "
            "If *file* is the character -, then a Nix expression will be read from standard input. "
            "Implies `--impure`.",
        .category = installablesCategory,
        .labels = {"file"},
        .handler = {&file},
        .completer = completePath
    });

    addFlag({
        .longName = "expr",
        .shortName = 'E',
        .description = "Interpret [*installables*](@docroot@/command-ref/new-cli/nix.md#installables) as attribute paths relative to the Nix expression *expr*.",
        .category = installablesCategory,
        .labels = {"expr"},
        .handler = {&expr}
    });
}

MixReadOnlyOption::MixReadOnlyOption()
{
    addFlag({
        .longName = "read-only",
        .description =
            "Do not instantiate each evaluated derivation. "
            "This improves performance, but can cause errors when accessing "
            "store paths of derivations during evaluation.",
        .handler = {&settings.readOnlyMode, true},
    });
}

Strings SourceExprCommand::getDefaultFlakeAttrPaths()
{
    return {
        "packages." + evalSettings.getCurrentSystem() + ".default",
        "defaultPackage." + evalSettings.getCurrentSystem()
    };
}

Strings SourceExprCommand::getDefaultFlakeAttrPathPrefixes()
{
    return {
        // As a convenience, look for the attribute in
        // 'outputs.packages'.
        "packages." + evalSettings.getCurrentSystem() + ".",
        // As a temporary hack until Nixpkgs is properly converted
        // to provide a clean 'packages' set, look in 'legacyPackages'.
        "legacyPackages." + evalSettings.getCurrentSystem() + "."
    };
}

Args::CompleterClosure SourceExprCommand::getCompleteInstallable()
{
    return [this](AddCompletions & completions, size_t, std::string_view prefix) {
        completeInstallable(*getEvaluator()->begin(aio()), completions, prefix);
    };
}

void SourceExprCommand::completeInstallable(EvalState & state, AddCompletions & completions, std::string_view prefix)
{
    try {
        if (file) {
            completions.setType(AddCompletions::Type::Attrs);

            auto evaluator = getEvaluator();

            Expr & e = evaluator->parseExprFromFile(state.ctx.paths.resolveExprPath(
                state.aio.blockOn(lookupFileArg(*evaluator, *file)).unwrap()
            ));

            Value root;
            state.eval(e, root);

            auto autoArgs = getAutoArgs(*evaluator);

            std::string prefix_ = std::string(prefix);
            auto sep = prefix_.rfind('.');
            std::string searchWord;
            if (sep != std::string::npos) {
                searchWord = prefix_.substr(sep + 1, std::string::npos);
                prefix_ = prefix_.substr(0, sep);
            } else {
                searchWord = prefix_;
                prefix_ = "";
            }

            auto [v1, pos] = findAlongAttrPath(state, prefix_, *autoArgs, root);
            state.forceValue(v1, pos);
            Value v2;
            state.autoCallFunction(*autoArgs, v1, v2, pos);

            if (v2.type() == nAttrs) {
                for (auto & i : *v2.attrs()) {
                    std::string name{evaluator->symbols[i.name]};
                    if (name.find(searchWord) == 0) {
                        if (prefix_ == "")
                            completions.add(name);
                        else
                            completions.add(prefix_ + "." + name);
                    }
                }
            }
        } else {
            completeFlakeRefWithFragment(
                completions,
                state,
                getEvaluator(),
                lockFlags,
                getDefaultFlakeAttrPathPrefixes(),
                getDefaultFlakeAttrPaths(),
                prefix);
        }
    } catch (EvalError&) {
        // Don't want eval errors to mess-up with the completion engine, so let's just swallow them
    }
}

void completeFlakeRefWithFragment(
    AddCompletions & completions,
    EvalState & evalState,
    ref<eval_cache::CachingEvaluator> evaluator,
    flake::LockFlags lockFlags,
    Strings attrPathPrefixes,
    const Strings & defaultFlakeAttrPaths,
    std::string_view prefix)
{
    /* Look for flake output attributes that match the
       prefix. */
    try {
        auto hash = prefix.find('#');
        if (hash == std::string::npos) {
            completeFlakeRef(evalState.aio, completions, evaluator->store, prefix);
        } else {
            completions.setType(AddCompletions::Type::Attrs);

            auto fragment = prefix.substr(hash + 1);
            std::string prefixRoot = "";
            if (fragment.starts_with(".")){
                fragment = fragment.substr(1);
                prefixRoot = ".";
            }
            auto flakeRefS = std::string(prefix.substr(0, hash));
            auto flakeRef = parseFlakeRef(expandTilde(flakeRefS), absPath("."));

            auto evalCache = openEvalCache(
                *evaluator,
                std::make_shared<flake::LockedFlake>(lockFlake(evalState, flakeRef, lockFlags))
            );

            auto root = evalCache->getRoot();

            if (prefixRoot == "."){
                attrPathPrefixes.clear();
            }
            /* Complete 'fragment' relative to all the
               attrpath prefixes as well as the root of the
               flake. */
            attrPathPrefixes.push_back("");

            for (auto & attrPathPrefixS : attrPathPrefixes) {
                auto attrPathPrefix = parseAttrPath(attrPathPrefixS);
                auto attrPathS = attrPathPrefixS + std::string(fragment);
                auto attrPath = parseAttrPath(attrPathS);

                std::string lastAttr;
                if (!attrPath.empty() && !attrPathS.ends_with(".")) {
                    lastAttr = attrPath.back();
                    attrPath.pop_back();
                }

                auto attr = root->findAlongAttrPath(evalState, attrPath);
                if (!attr) continue;

                for (auto & attr2 : (*attr)->getAttrs(evalState)) {
                    if (std::string_view attr2s = attr2; attr2s.starts_with(lastAttr)) {
                        auto attrPath2 = (*attr)->getAttrPath(evalState, attr2s);
                        /* Strip the attrpath prefix. */
                        attrPath2.erase(attrPath2.begin(), attrPath2.begin() + attrPathPrefix.size());
                        completions.add(flakeRefS + "#" + prefixRoot + concatStringsSep(".", attrPath2));
                    }
                }
            }

            /* And add an empty completion for the default
               attrpaths. */
            if (fragment.empty()) {
                for (auto & attrPath : defaultFlakeAttrPaths) {
                    auto attr = root->findAlongAttrPath(evalState, parseAttrPath(attrPath));
                    if (!attr) continue;
                    completions.add(flakeRefS + "#" + prefixRoot);
                }
            }
        }
    } catch (Error & e) {
        printTaggedWarning("%1%", Uncolored(e.msg()));
    }
}

void completeFlakeRef(
    AsyncIoRoot & aio, AddCompletions & completions, ref<Store> store, std::string_view prefix
)
{
    if (!experimentalFeatureSettings.isEnabled(Xp::Flakes))
        return;

    if (prefix == "")
        completions.add(".");

    Args::completeDir(completions, 0, prefix);

    /* Look for registry entries that match the prefix. */
    for (auto & registry : aio.blockOn(fetchers::getRegistries(store))) {
        for (auto & entry : registry->entries) {
            auto from = entry.from.to_string();
            if (!prefix.starts_with("flake:") && from.starts_with("flake:")) {
                std::string from2(from, 6);
                if (from2.starts_with(prefix))
                    completions.add(from2);
            } else {
                if (from.starts_with(prefix))
                    completions.add(from);
            }
        }
    }
}

DerivedPathWithInfo Installable::toDerivedPath(EvalState & state)
{
    auto buildables = toDerivedPaths(state);
    if (buildables.size() != 1)
        throw Error("installable '%s' evaluates to %d derivations, where only one is expected", what(), buildables.size());
    return std::move(buildables[0]);
}

static kj::Promise<Result<StorePath>> getDeriver(
    ref<Store> store,
    const Installable & i,
    const StorePath & drvPath)
try {
    auto derivers = TRY_AWAIT(store->queryValidDerivers(drvPath));
    if (derivers.empty())
        throw Error("'%s' does not have a known deriver", i.what());
    // FIXME: use all derivers?
    co_return *derivers.begin();
} catch (...) {
    co_return result::current_exception();
}

ref<eval_cache::EvalCache> openEvalCache(
    eval_cache::CachingEvaluator & state,
    std::shared_ptr<flake::LockedFlake> lockedFlake)
{
    auto fingerprint = evalSettings.useEvalCache && evalSettings.pureEval
        ? std::make_optional(lockedFlake->getFingerprint())
        : std::nullopt;
    auto rootLoader = [lockedFlake](EvalState & state)
        {
            /* For testing whether the evaluation cache is
               complete. */
            if (getEnv("NIX_ALLOW_EVAL").value_or("1") == "0")
                throw Error("not everything is cached, but evaluation is not allowed");

            Value vFlake;
            flake::callFlake(state, *lockedFlake, vFlake);

            state.forceAttrs(vFlake, noPos, "while parsing cached flake data");

            auto aOutputs = vFlake.attrs()->get(state.ctx.symbols.create("outputs"));
            assert(aOutputs);

            return aOutputs->value;
        };

    if (fingerprint) {
        return state.getCacheFor(fingerprint.value(), rootLoader);
    } else {
        return make_ref<nix::eval_cache::EvalCache>(std::nullopt, rootLoader);
    }
}

ref<eval_cache::CachingEvaluator> SourceExprCommand::getEvaluator()
{
    // FIXME: backward compatibility hack
    if (file) {
        evalSettings.pureEval.override(false);
    }

    return EvalCommand::getEvaluator();
}

Installables SourceExprCommand::parseInstallables(
    EvalState & state, ref<Store> store, std::vector<std::string> ss)
{
    Installables result;

    if (file || expr) {
        if (file && expr)
            throw UsageError("'--file' and '--expr' are exclusive");

        auto evaluator = getEvaluator();
        Value vFile;

        if (file == "-") {
            auto & e = evaluator->parseStdin();
            state.eval(e, vFile);
        }
        else if (file)
            state.evalFile(state.aio.blockOn(lookupFileArg(*evaluator, *file)).unwrap(), vFile);
        else {
            auto & e = evaluator->parseExprFromString(*expr, CanonPath::fromCwd());
            state.eval(e, vFile);
        }

        for (auto & s : ss) {
            auto [prefix, extendedOutputsSpec] = ExtendedOutputsSpec::parse(s);
            result.push_back(make_ref<InstallableAttrPath>(InstallableAttrPath::parse(
                evaluator, *this, vFile, std::move(prefix), std::move(extendedOutputsSpec)
            )));
        }

    } else {

        for (auto & s : ss) {
            std::exception_ptr ex;

            auto [prefix_, extendedOutputsSpec_] = ExtendedOutputsSpec::parse(s);
            // To avoid clang's pedantry
            auto prefix = std::move(prefix_);
            auto extendedOutputsSpec = std::move(extendedOutputsSpec_);

            if (prefix.find('/') != std::string::npos) {
                try {
                    result.push_back(make_ref<InstallableDerivedPath>(
                        InstallableDerivedPath::parse(store, prefix, extendedOutputsSpec.raw)));
                    continue;
                } catch (BadStorePath &) {
                } catch (...) {
                    if (!ex)
                        ex = std::current_exception();
                }
            }

            try {
                auto [flakeRef, fragment] = parseFlakeRefWithFragment(std::string { prefix }, absPath("."));
                result.push_back(make_ref<InstallableFlake>(
                        this,
                        getEvaluator(),
                        std::move(flakeRef),
                        fragment,
                        std::move(extendedOutputsSpec),
                        getDefaultFlakeAttrPaths(),
                        getDefaultFlakeAttrPathPrefixes(),
                        lockFlags));
                continue;
            } catch (...) {
                ex = std::current_exception();
            }

            std::rethrow_exception(ex);
        }
    }

    return result;
}

ref<Installable> SourceExprCommand::parseInstallable(
    EvalState & state, ref<Store> store, const std::string & installable)
{
    auto installables = parseInstallables(state, store, {installable});
    assert(installables.size() == 1);
    return installables.front();
}

std::vector<BuiltPathWithResult> Installable::build(
    EvalState & state,
    ref<Store> evalStore,
    ref<Store> store,
    Realise mode,
    const Installables & installables,
    BuildMode bMode)
{
    std::vector<BuiltPathWithResult> res;
    for (auto & [_, builtPathWithResult] : build2(state, evalStore, store, mode, installables, bMode))
        res.push_back(builtPathWithResult);
    return res;
}

static void throwBuildErrors(
    std::vector<KeyedBuildResult> & buildResults,
    const Store & store)
{
    std::vector<KeyedBuildResult> failed;
    for (auto & buildResult : buildResults) {
        if (!buildResult.success()) {
            failed.push_back(buildResult);
        }
    }

    auto failedResult = failed.begin();
    if (failedResult != failed.end()) {
        if (failed.size() == 1) {
            failedResult->rethrow();
        } else {
            StringSet failedPaths;
            for (; failedResult != failed.end(); failedResult++) {
                if (!failedResult->errorMsg.empty()) {
                    logError(ErrorInfo{
                        .level = lvlError,
                        .msg = failedResult->errorMsg,
                    });
                }
                failedPaths.insert(failedResult->path.to_string(store));
            }
            throw Error("build of %s failed", concatStringsSep(", ", quoteStrings(failedPaths)));
        }
    }
}

std::vector<std::pair<ref<Installable>, BuiltPathWithResult>> Installable::build2(
    EvalState & state,
    ref<Store> evalStore,
    ref<Store> store,
    Realise mode,
    const Installables & installables,
    BuildMode bMode)
{
    if (mode == Realise::Nothing)
        settings.readOnlyMode = true;

    struct Aux
    {
        ref<ExtraPathInfo> info;
        ref<Installable> installable;
    };

    std::vector<DerivedPath> pathsToBuild;
    std::map<DerivedPath, std::vector<Aux>> backmap;

    for (auto & i : installables) {
        for (auto b : i->toDerivedPaths(state)) {
            pathsToBuild.push_back(b.path);
            backmap[b.path].push_back({.info = b.info, .installable = i});
        }
    }

    std::vector<std::pair<ref<Installable>, BuiltPathWithResult>> res;

    switch (mode) {

    case Realise::Nothing:
    case Realise::Derivation:
        state.aio.blockOn(printMissing(store, pathsToBuild, lvlError));

        for (auto & path : pathsToBuild) {
            for (auto & aux : backmap[path]) {
                std::visit(overloaded {
                    [&](const DerivedPath::Built & bfd) {
                        auto outputs =
                            state.aio.blockOn(resolveDerivedPath(*store, bfd, &*evalStore));
                        res.push_back({aux.installable, {
                            .path = BuiltPath::Built {
                                .drvPath = bfd.drvPath,
                                .outputs = outputs,
                             },
                            .info = aux.info}});
                    },
                    [&](const DerivedPath::Opaque & bo) {
                        res.push_back({aux.installable, {
                            .path = BuiltPath::Opaque { bo.path },
                            .info = aux.info}});
                    },
                }, path.raw());
            }
        }

        break;

    case Realise::Outputs: {
        if (settings.printMissing)
            state.aio.blockOn(printMissing(store, pathsToBuild, lvlInfo));

        auto buildResults =
            state.aio.blockOn(store->buildPathsWithResults(pathsToBuild, bMode, evalStore));
        throwBuildErrors(buildResults, *store);
        for (auto & buildResult : buildResults) {
            for (auto & aux : backmap[buildResult.path]) {
                std::visit(overloaded {
                    [&](const DerivedPath::Built & bfd) {
                        std::map<std::string, StorePath> outputs;
                        for (auto & [outputName, realisation] : buildResult.builtOutputs)
                            outputs.emplace(outputName, realisation.outPath);
                        res.push_back({aux.installable, {
                            .path = BuiltPath::Built {
                                .drvPath = bfd.drvPath,
                                .outputs = outputs,
                            },
                            .info = aux.info,
                            .result = buildResult}});
                    },
                    [&](const DerivedPath::Opaque & bo) {
                        res.push_back({aux.installable, {
                            .path = BuiltPath::Opaque { bo.path },
                            .info = aux.info,
                            .result = buildResult}});
                    },
                }, buildResult.path.raw());
            }
        }

        break;
    }

    default:
        assert(false);
    }

    return res;
}

BuiltPaths Installable::toBuiltPaths(
    EvalState & state,
    ref<Store> evalStore,
    ref<Store> store,
    Realise mode,
    OperateOn operateOn,
    const Installables & installables)
{
    if (operateOn == OperateOn::Output) {
        BuiltPaths res;
        for (auto & p : Installable::build(state, evalStore, store, mode, installables))
            res.push_back(p.path);
        return res;
    } else {
        if (mode == Realise::Nothing)
            settings.readOnlyMode = true;

        BuiltPaths res;
        for (auto & drvPath : Installable::toDerivations(state, store, installables, true))
            res.emplace_back(BuiltPath::Opaque{drvPath});
        return res;
    }
}

StorePathSet Installable::toStorePathSet(
    EvalState & state,
    ref<Store> evalStore,
    ref<Store> store,
    Realise mode, OperateOn operateOn,
    const Installables & installables)
{
    StorePathSet outPaths;
    for (auto & path : toBuiltPaths(state, evalStore, store, mode, operateOn, installables)) {
        auto thisOutPaths = path.outPaths();
        outPaths.insert(thisOutPaths.begin(), thisOutPaths.end());
    }
    return outPaths;
}

StorePaths Installable::toStorePaths(
    EvalState & state,
    ref<Store> evalStore,
    ref<Store> store,
    Realise mode, OperateOn operateOn,
    const Installables & installables)
{
    StorePaths outPaths;
    for (auto & path : toBuiltPaths(state, evalStore, store, mode, operateOn, installables)) {
        auto thisOutPaths = path.outPaths();
        outPaths.insert(outPaths.end(), thisOutPaths.begin(), thisOutPaths.end());
    }
    return outPaths;
}

StorePath Installable::toStorePath(
    EvalState & state,
    ref<Store> evalStore,
    ref<Store> store,
    Realise mode, OperateOn operateOn,
    ref<Installable> installable)
{
    auto paths = toStorePathSet(state, evalStore, store, mode, operateOn, {installable});

    if (paths.size() != 1)
        throw Error("argument '%s' should evaluate to one store path", installable->what());

    return *paths.begin();
}

StorePathSet Installable::toDerivations(
    EvalState & state,
    ref<Store> store,
    const Installables & installables,
    bool useDeriver)
{
    StorePathSet drvPaths;

    for (const auto & i : installables)
        for (const auto & b : i->toDerivedPaths(state))
            std::visit(overloaded {
                [&](const DerivedPath::Opaque & bo) {
                    drvPaths.insert(
                        bo.path.isDerivation()
                            ? bo.path
                        : useDeriver
                            ? state.aio.blockOn(getDeriver(store, *i, bo.path))
                        : throw Error("argument '%s' did not evaluate to a derivation", i->what()));
                },
                [&](const DerivedPath::Built & bfd) {
                    drvPaths.insert(bfd.drvPath.path);
                },
            }, b.path.raw());

    return drvPaths;
}

RawInstallablesCommand::RawInstallablesCommand()
{
    addFlag({
        .longName = "stdin",
        .description = "Read installables from the standard input. No default installable applied.",
        .handler = {&readFromStdIn, true}
    });

    expectArgs({
        .label = "installables",
        .handler = {&rawInstallables},
        .completer = getCompleteInstallable(),
    });
}

void RawInstallablesCommand::applyDefaultInstallables(std::vector<std::string> & rawInstallables)
{
    if (rawInstallables.empty()) {
        // FIXME: commands like "nix profile install" should not have a
        // default, probably.
        rawInstallables.push_back(".");
    }
}

std::vector<FlakeRef> RawInstallablesCommand::getFlakeRefsForCompletion()
{
    applyDefaultInstallables(rawInstallables);
    std::vector<FlakeRef> res;
    for (auto i : rawInstallables)
        res.push_back(parseFlakeRefWithFragment(
            expandTilde(i),
            absPath(".")).first);
    return res;
}

void RawInstallablesCommand::run(ref<Store> store)
{
    if (readFromStdIn && !isatty(STDIN_FILENO)) {
        std::string word;
        while (std::cin >> word) {
            rawInstallables.emplace_back(std::move(word));
        }
    } else {
        applyDefaultInstallables(rawInstallables);
    }
    run(store, std::move(rawInstallables));
}

std::vector<FlakeRef> InstallableCommand::getFlakeRefsForCompletion()
{
    return {
        parseFlakeRefWithFragment(
            expandTilde(_installable),
            absPath(".")).first
    };
}

void InstallablesCommand::run(ref<Store> store, std::vector<std::string> && rawInstallables)
{
    auto installables = parseInstallables(*getEvaluator()->begin(aio()), store, rawInstallables);
    run(store, std::move(installables));
}

InstallableCommand::InstallableCommand()
    : SourceExprCommand()
{
    expectArgs({
        .label = "installable",
        .optional = true,
        .handler = {&_installable},
        .completer = getCompleteInstallable(),
    });
}

void InstallableCommand::run(ref<Store> store)
{
    auto installable = parseInstallable(*getEvaluator()->begin(aio()), store, _installable);
    run(store, std::move(installable));
}

void BuiltPathsCommand::applyDefaultInstallables(std::vector<std::string> & rawInstallables)
{
    if (rawInstallables.empty() && !all)
        rawInstallables.push_back(".");
}

}
