#include "lix/libcmd/command.hh"
#include "lix/libcmd/installable-flake.hh"
#include "lix/libmain/common-args.hh"
#include "lix/libmain/shared.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/eval-inline.hh" // IWYU pragma: keep
#include "lix/libexpr/eval-settings.hh"
#include "lix/libexpr/flake/flake.hh"
#include "lix/libexpr/get-drvs.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libstore/outputs-spec.hh"
#include "lix/libexpr/attr-path.hh"
#include "lix/libfetchers/fetchers.hh"
#include "lix/libfetchers/registry.hh"
#include "lix/libexpr/eval-cache.hh"
#include "lix/libcmd/markdown.hh"
#include "lix/libutil/json.hh"
#include "lix/libutil/terminal.hh"
#include "lix/libutil/signals.hh"
#include "flake.hh"
#include "lix/libutil/types.hh"

#include <limits>
#include <iomanip>

namespace nix {
using namespace nix::flake;

struct CmdFlakeUpdate;
class FlakeCommand : public virtual Args, public MixFlakeOptions
{
protected:
    std::string flakeUrl = ".";

public:

    FlakeCommand()
    {
        expectArgs({
            .label = "flake-url",
            .optional = true,
            .handler = {&flakeUrl},
            .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
                completeFlakeRef(aio(), completions, getStore(), prefix);
            }}
        });
    }

    FlakeRef getFlakeRef()
    {
        return parseFlakeRef(flakeUrl, absPath(".")); //FIXME
    }

    LockedFlake lockFlake(EvalState & state)
    {
        return flake::lockFlake(state, getFlakeRef(), lockFlags);
    }

    std::vector<FlakeRef> getFlakeRefsForCompletion() override
    {
        return {
            // Like getFlakeRef but with expandTilde calld first
            parseFlakeRef(expandTilde(flakeUrl), absPath("."))
        };
    }
};

struct CmdFlakeUpdate : FlakeCommand
{
public:

    std::string description() override
    {
        return "update flake lock file";
    }

    CmdFlakeUpdate()
    {
        expectedArgs.clear();
        addFlag({
            .longName="flake",
            .description="The flake to operate on. Default is the current directory.",
            .labels={"flake-url"},
            .handler={&flakeUrl},
            .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
                completeFlakeRef(aio(), completions, getStore(), prefix);
            }}
        });
        expectArgs({
            .label="inputs",
            .optional=true,
            .handler={[&](std::vector<std::string> inputsToUpdate) {
                for (const auto & inputToUpdate : inputsToUpdate) {
                    auto inputPath = flake::parseInputPath(inputToUpdate);
                    if (lockFlags.inputUpdates.contains(inputPath))
                        printTaggedWarning(
                            "Input '%s' was specified multiple times. You may have done this by "
                            "accident.",
                            inputToUpdate
                        );
                    lockFlags.inputUpdates.insert(inputPath);
                }
            }},
            .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
                completeFlakeInputPath(
                    completions, *getEvaluator()->begin(aio()), getFlakeRefsForCompletion(), prefix
                );
            }}
        });

        /* Remove flags that don't make sense. */
        removeFlag("no-update-lock-file");
        removeFlag("no-write-lock-file");
    }

    std::string doc() override
    {
        return
          #include "flake-update.md"
          ;
    }

    void run(nix::ref<nix::Store> store) override
    {
        settings.tarballTtl.override(0);
        auto updateAll = lockFlags.inputUpdates.empty();

        lockFlags.recreateLockFile = updateAll;
        lockFlags.writeLockFile = true;
        lockFlags.applyNixConfig = true;

        lockFlake(*getEvaluator()->begin(aio()));
    }
};

struct CmdFlakeLock : FlakeCommand
{
    std::string description() override
    {
        return "create missing lock file entries";
    }

    CmdFlakeLock()
    {
        addFlag({
            .longName="update-input",
            .description="Replaced with `nix flake update input...`",
            .labels={"input-path"},
            .handler={[&](std::string inputToUpdate){
                throw UsageError("`nix flake lock --update-input %1%` has been replaced by `nix flake update %1%`", inputToUpdate);
            }}
        });

        /* Remove flags that don't make sense. */
        removeFlag("no-write-lock-file");
    }

    std::string doc() override
    {
        return
          #include "flake-lock.md"
          ;
    }

    void run(nix::ref<nix::Store> store) override
    {
        settings.tarballTtl.override(0);

        lockFlags.writeLockFile = true;
        lockFlags.applyNixConfig = true;

        lockFlake(*getEvaluator()->begin(aio()));
    }
};

static void enumerateOutputs(
    EvalState & state,
    Value & vFlake,
    std::function<void(const std::string_view name, Value & vProvide, const PosIdx pos)> callback
)
{
    state.forceAttrs(vFlake, noPos, "while evaluating a flake to get its outputs");

    auto aOutputs = vFlake.attrs()->get(state.ctx.symbols.create("outputs"));
    assert(aOutputs);

    state.forceAttrs(aOutputs->value, noPos, "while evaluating the outputs of a flake");

    auto sHydraJobs = state.ctx.symbols.create("hydraJobs");

    /* Hack: ensure that hydraJobs is evaluated before anything
       else. This way we can disable IFD for hydraJobs and then enable
       it for other outputs. */
    if (auto attr = aOutputs->value.attrs()->get(sHydraJobs)) {
        callback(state.ctx.symbols[attr->name], attr->value, attr->pos);
    }

    for (auto & attr : *aOutputs->value.attrs()) {
        if (attr.name != sHydraJobs) {
            callback(state.ctx.symbols[attr.name], attr.value, attr.pos);
        }
    }
}

struct CmdFlakeMetadata : FlakeCommand, MixJSON
{
    std::string description() override
    {
        return "show flake metadata";
    }

    std::string doc() override
    {
        return
          #include "flake-metadata.md"
          ;
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto lockedFlake = lockFlake(*getEvaluator()->begin(aio()));
        auto & flake = lockedFlake.flake;
        auto formatTime = [](time_t time) -> std::string {
            std::ostringstream os{};
            os << std::put_time(std::localtime(&time), "%F %T");
            return os.str();
        };

        if (json) {
            JSON j;
            if (flake.description)
                j["description"] = *flake.description;
            j["originalUrl"] = flake.originalRef.to_string();
            j["original"] = fetchers::attrsToJSON(flake.originalRef.toAttrs());
            j["resolvedUrl"] = flake.resolvedRef.to_string();
            j["resolved"] = fetchers::attrsToJSON(flake.resolvedRef.toAttrs());
            j["url"] = flake.lockedRef.to_string(); // FIXME: rename to lockedUrl
            j["locked"] = fetchers::attrsToJSON(flake.lockedRef.toAttrs());
            if (auto rev = flake.lockedRef.input.getRev())
                j["revision"] = rev->to_string(Base::Base16, false);
            if (auto dirtyRev = fetchers::maybeGetStrAttr(flake.lockedRef.toAttrs(), "dirtyRev"))
                j["dirtyRevision"] = *dirtyRev;
            if (auto revCount = flake.lockedRef.input.getRevCount())
                j["revCount"] = *revCount;
            if (auto lastModified = flake.lockedRef.input.getLastModified())
                j["lastModified"] = *lastModified;
            j["path"] = store->printStorePath(flake.sourceInfo->storePath);
            j["locks"] = lockedFlake.lockFile.toJSON();
            logger->cout("%s", j.dump());
        } else {
            logger->cout(
                ANSI_BOLD "Resolved URL:" ANSI_NORMAL "  %s",
                flake.resolvedRef.to_string());
            logger->cout(
                ANSI_BOLD "Locked URL:" ANSI_NORMAL "    %s",
                flake.lockedRef.to_string());
            if (flake.description)
                logger->cout(
                    ANSI_BOLD "Description:" ANSI_NORMAL "   %s",
                    *flake.description);
            logger->cout(
                ANSI_BOLD "Path:" ANSI_NORMAL "          %s",
                store->printStorePath(flake.sourceInfo->storePath));
            if (auto rev = flake.lockedRef.input.getRev())
                logger->cout(
                    ANSI_BOLD "Revision:" ANSI_NORMAL "      %s",
                    rev->to_string(Base::Base16, false));
            if (auto dirtyRev = fetchers::maybeGetStrAttr(flake.lockedRef.toAttrs(), "dirtyRev"))
                logger->cout(
                    ANSI_BOLD "Revision:" ANSI_NORMAL "      %s",
                    *dirtyRev);
            if (auto revCount = flake.lockedRef.input.getRevCount())
                logger->cout(
                    ANSI_BOLD "Revisions:" ANSI_NORMAL "     %s",
                    *revCount);
            if (auto lastModified = flake.lockedRef.input.getLastModified())
                logger->cout(
                    ANSI_BOLD "Last modified:" ANSI_NORMAL " %s",
                    formatTime(*lastModified));

            if (!lockedFlake.lockFile.root->inputs.empty())
                logger->cout(ANSI_BOLD "Inputs:" ANSI_NORMAL);

            std::set<ref<Node>> visited;

            std::function<void(const Node & node, const std::string & prefix)> recurse;

            recurse = [&](const Node & node, const std::string & prefix)
            {
                for (const auto & [i, input] : enumerate(node.inputs)) {
                    bool last = i + 1 == node.inputs.size();

                    if (auto lockedNode = std::get_if<0>(&input.second)) {
                        // ├───agenix: github:ryantm/agenix/8d37c5bdeade12b6479c85acd133063ab53187a0
                        logger->cout("%s%s" ANSI_BOLD "%s" ANSI_NORMAL ": %s",
                            prefix, last ? treeLast : treeConn, input.first,
                            (*lockedNode)->lockedRef);

                        // ├───lix: https://git.lix.systems/api/v1/repos/lix-project <....>
                        // │   Last modified: 2024-07-31 21:01:34
                        if (auto lastModified = (*lockedNode)->lockedRef.input.getLastModified()) {
                            logger->cout("%s%s" ANSI_BOLD "%s" ANSI_NORMAL ": %s",
                                prefix, last ? treeNull : treeLine, "Last modified", formatTime(*lastModified));
                        }

                        bool firstVisit = visited.insert(*lockedNode).second;

                        if (firstVisit) recurse(**lockedNode, prefix + (last ? treeNull : treeLine));
                    } else if (auto follows = std::get_if<1>(&input.second)) {
                        // │   ├───darwin follows input 'flake-utils'
                        logger->cout("%s%s" ANSI_BOLD "%s" ANSI_NORMAL " follows input '%s'",
                            prefix, last ? treeLast : treeConn, input.first,
                            printInputPath(*follows));
                    }
                }
            };

            visited.insert(lockedFlake.lockFile.root);
            recurse(*lockedFlake.lockFile.root, "");
        }
    }
};

struct CmdFlakeInfo : CmdFlakeMetadata
{
    void run(nix::ref<nix::Store> store) override
    {
        printTaggedWarning("'nix flake info' is a deprecated alias for 'nix flake metadata'");
        CmdFlakeMetadata::run(store);
    }
};

struct CmdFlakeCheck : FlakeCommand
{
    bool build = true;
    bool checkAllSystems = false;

    CmdFlakeCheck()
    {
        addFlag({
            .longName = "no-build",
            .description = "Do not build checks.",
            .handler = {&build, false}
        });
        addFlag({
            .longName = "all-systems",
            .description = "Check the outputs for all systems.",
            .handler = {&checkAllSystems, true}
        });
    }

    std::string description() override
    {
        return "check whether the flake evaluates and run its tests";
    }

    std::string doc() override
    {
        return
          #include "flake-check.md"
          ;
    }

    void run(nix::ref<nix::Store> store) override
    {
        if (!build) {
            settings.readOnlyMode = true;
            evalSettings.enableImportFromDerivation.setDefault(false);
        }

        auto evaluator = getEvaluator();
        auto state = evaluator->begin(aio());

        lockFlags.applyNixConfig = true;
        auto flake = lockFlake(*state);
        auto localSystem = std::string(evalSettings.getCurrentSystem());

        bool hasErrors = false;
        auto reportError = [&](const Error & e) {
            try {
                throw e;
            } catch (Interrupted & e) {
                throw;
            } catch (Error & e) {
                if (settings.keepGoing) {
                    ignoreExceptionExceptInterrupt();
                    hasErrors = true;
                }
                else
                    throw;
            }
        };

        std::set<std::string> omittedSystems;

        // FIXME: rewrite to use EvalCache.

        auto resolve = [&] (PosIdx p) {
            return evaluator->positions[p];
        };

        auto checkSystemName = [&](const std::string_view system, const PosIdx pos) {
            // FIXME: what's the format of "system"?
            if (system.find('-') == std::string::npos)
                reportError(Error("'%s' is not a valid system type, at %s", system, resolve(pos)));
        };

        auto checkSystemType = [&](const std::string_view system, const PosIdx pos) {
            if (!checkAllSystems && system != localSystem) {
                omittedSystems.emplace(system);
                return false;
            } else {
                return true;
            }
        };

        auto checkDerivation = [&](const std::string & attrPath, Value & v, const PosIdx pos) -> std::optional<StorePath> {
            try {
                Activity act(*logger, lvlInfo, actUnknown,
                    fmt("checking derivation %s", attrPath));
                auto drvInfo = getDerivation(*state, v, false);
                if (!drvInfo)
                    throw Error("flake attribute '%s' is not a derivation", attrPath);
                else {
                    // FIXME: check meta attributes
                    auto storePath = drvInfo->queryDrvPath(*state);
                    if (storePath) {
                        printInfo(
                            "derivation evaluated to %s", store->printStorePath(storePath.value())
                        );
                    }
                    return storePath;
                }
            } catch (Error & e) {
                e.addTrace(resolve(pos), HintFmt("while checking the derivation '%s'", attrPath));
                reportError(e);
            }
            return std::nullopt;
        };

        std::vector<DerivedPath> drvPaths;

        auto checkApp = [&](const std::string & attrPath, Value & v, const PosIdx pos) {
            try {
                #if 0
                // FIXME
                auto app = App(*state, v);
                for (auto & i : app.context) {
                    auto [drvPathS, outputName] = NixStringContextElem::parse(i);
                    store->parseStorePath(drvPathS);
                }
                #endif
            } catch (Error & e) {
                e.addTrace(resolve(pos), HintFmt("while checking the app definition '%s'", attrPath));
                reportError(e);
            }
        };

        auto checkOverlay = [&](const std::string_view attrPath, Value & v, const PosIdx pos) {
            try {
                Activity act(*logger, lvlInfo, actUnknown,
                    fmt("checking overlay '%s'", attrPath));
                state->forceValue(v, pos);
                if (!v.isLambda()) {
                    throw Error("overlay is not a function, but %s instead", showType(v));
                }
                auto body = v.lambda().fun->body->try_cast<ExprLambda>();
                if (!body)
                    throw Error("overlay is not a function with two arguments, but only takes one");
                if (body->body->try_cast<ExprLambda>())
                    throw Error("overlay is not a function with two arguments, but takes more than two");
                // FIXME: if we have a 'nixpkgs' input, use it to
                // evaluate the overlay.
            } catch (Error & e) {
                e.addTrace(resolve(pos), HintFmt("while checking the overlay '%s'", attrPath));
                reportError(e);
            }
        };

        auto checkModule = [&](const std::string_view attrPath, Value & v, const PosIdx pos) {
            try {
                Activity act(*logger, lvlInfo, actUnknown,
                    fmt("checking NixOS module '%s'", attrPath));
                state->forceValue(v, pos);
            } catch (Error & e) {
                e.addTrace(resolve(pos), HintFmt("while checking the NixOS module '%s'", attrPath));
                reportError(e);
            }
        };

        std::function<void(const std::string_view attrPath, Value & v, const PosIdx pos)>
            checkHydraJobs;

        checkHydraJobs = [&](const std::string_view attrPath, Value & v, const PosIdx pos) {
            try {
                Activity act(*logger, lvlInfo, actUnknown,
                    fmt("checking Hydra job '%s'", attrPath));
                state->forceAttrs(v, pos, "");

                if (state->isDerivation(v))
                    throw Error("jobset should not be a derivation at top-level");

                for (auto & attr : *v.attrs()) {
                    state->forceAttrs(attr.value, attr.pos, "");
                    auto attrPath2 = concatStrings(attrPath, ".", evaluator->symbols[attr.name]);
                    if (state->isDerivation(attr.value)) {
                        Activity act(*logger, lvlInfo, actUnknown,
                            fmt("checking Hydra job '%s'", attrPath2));
                        checkDerivation(attrPath2, attr.value, attr.pos);
                    } else {
                        checkHydraJobs(attrPath2, attr.value, attr.pos);
                    }
                }

            } catch (Error & e) {
                e.addTrace(resolve(pos), HintFmt("while checking the Hydra jobset '%s'", attrPath));
                reportError(e);
            }
        };

        auto checkNixOSConfiguration = [&](const std::string & attrPath, Value & v, const PosIdx pos) {
            try {
                Activity act(*logger, lvlInfo, actUnknown,
                    fmt("checking NixOS configuration '%s'", attrPath));
                Bindings & bindings(*evaluator->mem.allocBindings(0));
                auto vToplevel = findAlongAttrPath(*state, "config.system.build.toplevel", bindings, v).first;
                state->forceValue(vToplevel, pos);
                if (!state->isDerivation(vToplevel)) {
                    throw Error("attribute 'config.system.build.toplevel' is not a derivation");
                }
            } catch (Error & e) {
                e.addTrace(resolve(pos), HintFmt("while checking the NixOS configuration '%s'", attrPath));
                reportError(e);
            }
        };

        auto checkTemplate = [&](const std::string_view attrPath, Value & v, const PosIdx pos) {
            try {
                Activity act(*logger, lvlInfo, actUnknown,
                    fmt("checking template '%s'", attrPath));

                state->forceAttrs(v, pos, "");

                if (auto attr = v.attrs()->get(evaluator->symbols.create("path"))) {
                    if (attr->name == evaluator->symbols.create("path")) {
                        NixStringContext context;
                        auto path = state->ctx.paths.checkSourcePath(
                            state->coerceToPath(attr->pos, attr->value, context, "")
                        );
                        if (!path.pathExists())
                            throw Error("template '%s' refers to a non-existent path '%s'", attrPath, path);
                        // TODO: recursively check the flake in 'path'.
                    }
                } else
                    throw Error("template '%s' lacks attribute 'path'", attrPath);

                if (auto attr = v.attrs()->get(evaluator->symbols.create("description")))
                    state->forceStringNoCtx(attr->value, attr->pos, "");
                else
                    throw Error("template '%s' lacks attribute 'description'", attrPath);

                for (auto & attr : *v.attrs()) {
                    std::string_view name(evaluator->symbols[attr.name]);
                    if (name != "path" && name != "description" && name != "welcomeText")
                        throw Error("template '%s' has unsupported attribute '%s'", attrPath, name);
                }
            } catch (Error & e) {
                e.addTrace(resolve(pos), HintFmt("while checking the template '%s'", attrPath));
                reportError(e);
            }
        };

        auto checkBundler = [&](const std::string & attrPath, Value & v, const PosIdx pos) {
            try {
                Activity act(*logger, lvlInfo, actUnknown,
                    fmt("checking bundler '%s'", attrPath));
                state->forceValue(v, pos);
                if (!v.isLambda())
                    throw Error("bundler must be a function");
                // TODO: check types of inputs/outputs?
            } catch (Error & e) {
                e.addTrace(resolve(pos), HintFmt("while checking the template '%s'", attrPath));
                reportError(e);
            }
        };

        {
            Activity act(*logger, lvlInfo, actUnknown, "evaluating flake");

            Value vFlake;
            flake::callFlake(*state, flake, vFlake);

            enumerateOutputs(
                *state,
                vFlake,
                [&](const std::string_view name, Value & vOutput, const PosIdx pos) {
                    Activity act(*logger, lvlInfo, actUnknown,
                        fmt("checking flake output '%s'", name));

                    try {
                        evalSettings.enableImportFromDerivation.setDefault(name != "hydraJobs");

                        state->forceValue(vOutput, pos);

                        std::string_view replacement =
                            name == "defaultPackage" ? "packages.<system>.default" :
                            name == "defaultApp" ? "apps.<system>.default" :
                            name == "defaultTemplate" ? "templates.default" :
                            name == "defaultBundler" ? "bundlers.<system>.default" :
                            name == "overlay" ? "overlays.default" :
                            name == "devShell" ? "devShells.<system>.default" :
                            name == "nixosModule" ? "nixosModules.default" :
                            "";
                        if (replacement != "")
                            printTaggedWarning(
                                "flake output attribute '%s' is deprecated; use '%s' instead",
                                name,
                                replacement
                            );

                        if (name == "checks") {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs()) {
                                const auto & attr_name = evaluator->symbols[attr.name];
                                checkSystemName(attr_name, attr.pos);
                                if (checkSystemType(attr_name, attr.pos)) {
                                    state->forceAttrs(attr.value, attr.pos, "");
                                    for (auto & attr2 : *attr.value.attrs()) {
                                        auto drvPath = checkDerivation(
                                            fmt("%s.%s.%s",
                                                name,
                                                attr_name,
                                                evaluator->symbols[attr2.name]),
                                            attr2.value,
                                            attr2.pos
                                        );
                                        if (drvPath && attr_name == evalSettings.getCurrentSystem()) {
                                            drvPaths.push_back(DerivedPath::Built {
                                                .drvPath = makeConstantStorePath(*drvPath),
                                                .outputs = OutputsSpec::All { },
                                            });
                                        }
                                    }
                                }
                            }
                        }

                        else if (name == "formatter")
                        {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs()) {
                                const auto & attr_name = evaluator->symbols[attr.name];
                                checkSystemName(attr_name, attr.pos);
                                if (checkSystemType(attr_name, attr.pos)) {
                                    checkApp(fmt("%s.%s", name, attr_name), attr.value, attr.pos);
                                };
                            }
                        }

                        else if (name == "packages" || name == "devShells")
                        {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs()) {
                                const auto & attr_name = evaluator->symbols[attr.name];
                                checkSystemName(attr_name, attr.pos);
                                if (checkSystemType(attr_name, attr.pos)) {
                                    state->forceAttrs(attr.value, attr.pos, "");
                                    for (auto & attr2 : *attr.value.attrs()) {
                                        checkDerivation(
                                            fmt("%s.%s.%s",
                                                name,
                                                attr_name,
                                                evaluator->symbols[attr2.name]),
                                            attr2.value,
                                            attr2.pos
                                        );
                                    }
                                };
                            }
                        }

                        else if (name == "apps")
                        {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs()) {
                                const auto & attr_name = evaluator->symbols[attr.name];
                                checkSystemName(attr_name, attr.pos);
                                if (checkSystemType(attr_name, attr.pos)) {
                                    state->forceAttrs(attr.value, attr.pos, "");
                                    for (auto & attr2 : *attr.value.attrs()) {
                                        checkApp(
                                            fmt("%s.%s.%s",
                                                name,
                                                attr_name,
                                                evaluator->symbols[attr2.name]),
                                            attr2.value,
                                            attr2.pos
                                        );
                                    }
                                };
                            }
                        }

                        else if (name == "defaultPackage" || name == "devShell")
                        {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs()) {
                                const auto & attr_name = evaluator->symbols[attr.name];
                                checkSystemName(attr_name, attr.pos);
                                if (checkSystemType(attr_name, attr.pos)) {
                                    checkDerivation(
                                        fmt("%s.%s", name, attr_name), attr.value, attr.pos
                                    );
                                };
                            }
                        }

                        else if (name == "defaultApp")
                        {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs()) {
                                const auto & attr_name = evaluator->symbols[attr.name];
                                checkSystemName(attr_name, attr.pos);
                                if (checkSystemType(attr_name, attr.pos) ) {
                                    checkApp(fmt("%s.%s", name, attr_name), attr.value, attr.pos);
                                };
                            }
                        }

                        else if (name == "legacyPackages")
                        {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs()) {
                                checkSystemName(evaluator->symbols[attr.name], attr.pos);
                                checkSystemType(evaluator->symbols[attr.name], attr.pos);
                                // FIXME: do getDerivations?
                            }
                        }

                        else if (name == "overlay")
                        {
                            checkOverlay(name, vOutput, pos);
                        }

                        else if (name == "overlays")
                        {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs())
                                checkOverlay(
                                    fmt("%s.%s", name, evaluator->symbols[attr.name]),
                                    attr.value,
                                    attr.pos
                                );
                        }

                        else if (name == "nixosModule")
                        {
                            checkModule(name, vOutput, pos);
                        }

                        else if (name == "nixosModules")
                        {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs())
                                checkModule(
                                    fmt("%s.%s", name, evaluator->symbols[attr.name]),
                                    attr.value,
                                    attr.pos
                                );
                        }

                        else if (name == "nixosConfigurations")
                        {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs())
                                checkNixOSConfiguration(
                                    fmt("%s.%s", name, evaluator->symbols[attr.name]),
                                    attr.value,
                                    attr.pos
                                );
                        }

                        else if (name == "hydraJobs")
                        {
                            checkHydraJobs(name, vOutput, pos);
                        }

                        else if (name == "defaultTemplate")
                        {
                            checkTemplate(name, vOutput, pos);
                        }

                        else if (name == "templates")
                        {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs())
                                checkTemplate(
                                    fmt("%s.%s", name, evaluator->symbols[attr.name]),
                                    attr.value,
                                    attr.pos
                                );
                        }

                        else if (name == "defaultBundler")
                        {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs()) {
                                const auto & attr_name = evaluator->symbols[attr.name];
                                checkSystemName(attr_name, attr.pos);
                                if (checkSystemType(attr_name, attr.pos)) {
                                    checkBundler(
                                        fmt("%s.%s", name, attr_name), attr.value, attr.pos
                                    );
                                };
                            }
                        }

                        else if (name == "bundlers")
                        {
                            state->forceAttrs(vOutput, pos, "");
                            for (auto & attr : *vOutput.attrs()) {
                                const auto & attr_name = evaluator->symbols[attr.name];
                                checkSystemName(attr_name, attr.pos);
                                if (checkSystemType(attr_name, attr.pos)) {
                                    state->forceAttrs(attr.value, attr.pos, "");
                                    for (auto & attr2 : *attr.value.attrs()) {
                                        checkBundler(
                                            fmt("%s.%s.%s",
                                                name,
                                                attr_name,
                                                evaluator->symbols[attr2.name]),
                                            attr2.value,
                                            attr2.pos
                                        );
                                    }
                                };
                            }
                        }

                        else if (name == "lib" || name == "darwinConfigurations"
                                 || name == "darwinModules" || name == "flakeModule"
                                 || name == "flakeModules" || name == "herculesCI"
                                 || name == "homeConfigurations" || name == "nixopsConfigurations")
                            // Known but unchecked community attribute
                            ;

                        else
                            printTaggedWarning("unknown flake output '%s'", name);

                    } catch (Error & e) {
                        e.addTrace(resolve(pos), HintFmt("while checking flake output '%s'", name));
                        reportError(e);
                    }
                }
            );
        }

        if (build && !drvPaths.empty()) {
            Activity act(*logger, lvlInfo, actUnknown,
                fmt("running %d flake checks", drvPaths.size()));
            aio().blockOn(store->buildPaths(drvPaths));
        }
        if (hasErrors)
            throw Error("some errors were encountered during the evaluation");

        if (!omittedSystems.empty()) {
            printTaggedWarning(
                "The check omitted these incompatible systems: %s\n"
                "Use '--all-systems' to check all.",
                concatStringsSep(", ", omittedSystems)
            );
        };
    };
};

static Strings defaultTemplateAttrPathsPrefixes{"templates."};
static Strings defaultTemplateAttrPaths = {"templates.default", "defaultTemplate"};

struct CmdFlakeInitCommon : virtual Args, EvalCommand
{
    std::string templateUrl = "templates";
    Path destDir;

    const LockFlags lockFlags{ .writeLockFile = false };

    CmdFlakeInitCommon()
    {
        addFlag({
            .longName = "template",
            .shortName = 't',
            .description = "The template to use.",
            .labels = {"template"},
            .handler = {&templateUrl},
            .completer = {[&](AddCompletions & completions, size_t, std::string_view prefix) {
                completeFlakeRefWithFragment(
                    completions,
                    *getEvaluator()->begin(aio()),
                    getEvaluator(),
                    lockFlags,
                    defaultTemplateAttrPathsPrefixes,
                    defaultTemplateAttrPaths,
                    prefix);
            }}
        });
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto flakeDir = absPath(destDir);

        auto evaluator = getEvaluator();
        auto evalState = evaluator->begin(aio());

        auto [templateFlakeRef, templateName] = parseFlakeRefWithFragment(templateUrl, absPath("."));

        auto installable = InstallableFlake(nullptr,
            evaluator, std::move(templateFlakeRef), templateName, ExtendedOutputsSpec::Default(),
            defaultTemplateAttrPaths,
            defaultTemplateAttrPathsPrefixes,
            lockFlags);

        auto cursor = installable.getCursor(*evalState);

        auto templateDirAttr = cursor->getAttr(*evalState, "path");
        auto templateDir = templateDirAttr->getString(*evalState);

        if (!store->isInStore(templateDir))
            evaluator->errors.make<TypeError>(
                "'%s' was not found in the Nix store\n"
                "If you've set '%s' to a string, try using a path instead.",
                templateDir, templateDirAttr->getAttrPathStr(*evalState)).debugThrow();

        std::vector<Path> changedFiles;
        std::vector<Path> conflictedFiles;

        std::function<void(const Path & from, const Path & to)> copyDir;
        copyDir = [&](const Path & from, const Path & to)
        {
            createDirs(to);

            for (auto & entry : readDirectory(from)) {
                auto from2 = from + "/" + entry.name;
                auto to2 = to + "/" + entry.name;
                auto st = lstat(from2);
                if (S_ISDIR(st.st_mode))
                    copyDir(from2, to2);
                else if (S_ISREG(st.st_mode)) {
                    auto contents = readFile(from2);
                    if (pathExists(to2)) {
                        auto contents2 = readFile(to2);
                        if (contents != contents2) {
                            printError("refusing to overwrite existing file '%s'\n please merge it manually with '%s'", to2, from2);
                            conflictedFiles.push_back(to2);
                        } else {
                            notice("skipping identical file: %s", from2);
                        }
                        continue;
                    } else
                        writeFile(to2, contents);
                }
                else if (S_ISLNK(st.st_mode)) {
                    auto target = readLink(from2);
                    if (pathExists(to2)) {
                        if (readLink(to2) != target) {
                            printError("refusing to overwrite existing file '%s'\n please merge it manually with '%s'", to2, from2);
                            conflictedFiles.push_back(to2);
                        } else {
                            notice("skipping identical file: %s", from2);
                        }
                        continue;
                    } else
                          createSymlink(target, to2);
                }
                else
                    throw Error("file '%s' has unsupported type", from2);
                changedFiles.push_back(to2);
                notice("wrote: %s", to2);
            }
        };

        copyDir(templateDir, flakeDir);

        if (!changedFiles.empty() && pathExists(flakeDir + "/.git")) {
            Strings args = { "-C", flakeDir, "add", "--intent-to-add", "--force", "--" };
            for (auto & s : changedFiles) args.push_back(s);
            aio().blockOn(runProgram("git", true, args));
        }
        auto welcomeText = cursor->maybeGetAttr(*evalState, "welcomeText");
        if (welcomeText) {
            notice(
                "\n%1%",
                Uncolored(renderMarkdownToTerminal(
                    welcomeText->getString(*evalState), StandardOutputStream::Stderr
                ))
            );
        }

        if (!conflictedFiles.empty())
            throw Error("Encountered %d conflicts - see above", conflictedFiles.size());
    }
};

struct CmdFlakeInit : CmdFlakeInitCommon
{
    std::string description() override
    {
        return "create a flake in the current directory from a template";
    }

    std::string doc() override
    {
        return
          #include "flake-init.md"
          ;
    }

    CmdFlakeInit()
    {
        destDir = ".";
    }
};

struct CmdFlakeNew : CmdFlakeInitCommon
{
    std::string description() override
    {
        return "create a flake in the specified directory from a template";
    }

    std::string doc() override
    {
        return
          #include "flake-new.md"
          ;
    }

    CmdFlakeNew()
    {
        expectArgs({
            .label = "dest-dir",
            .handler = {&destDir},
            .completer = completePath
        });
    }
};

struct CmdFlakeClone : FlakeCommand
{
    Path destDir;

    std::string description() override
    {
        return "clone flake repository";
    }

    std::string doc() override
    {
        return
          #include "flake-clone.md"
          ;
    }

    CmdFlakeClone()
    {
        addFlag({
            .longName = "dest",
            .shortName = 'f',
            .description = "Clone the flake to path *dest*.",
            .labels = {"path"},
            .handler = {&destDir}
        });
    }

    void run(nix::ref<nix::Store> store) override
    {
        if (destDir.empty())
            throw Error("missing flag '--dest'");

        aio().blockOn(aio().blockOn(getFlakeRef().resolve(store)).input.clone(destDir));
    }
};

struct CmdFlakeArchive : FlakeCommand, MixJSON, MixDryRun
{
    std::string dstUri;

    CmdFlakeArchive()
    {
        addFlag({
            .longName = "to",
            .description = "URI of the destination Nix store",
            .labels = {"store-uri"},
            .handler = {&dstUri}
        });
    }

    std::string description() override
    {
        return "copy a flake and all its inputs to a store";
    }

    std::string doc() override
    {
        return
          #include "flake-archive.md"
          ;
    }

    void run(nix::ref<nix::Store> store) override
    {
        auto flake = lockFlake(*getEvaluator()->begin(aio()));

        StorePathSet sources;

        sources.insert(flake.flake.sourceInfo->storePath);

        // FIXME: use graph output, handle cycles.
        std::function<JSON(const Node & node)> traverse;
        traverse = [&](const Node & node)
        {
            JSON jsonObj2 = json ? JSON::object() : JSON(nullptr);
            for (auto & [inputName, input] : node.inputs) {
                if (auto inputNode = std::get_if<0>(&input)) {
                    auto storePath =
                        dryRun
                        ? (*inputNode)->lockedRef.input.computeStorePath(*store)
                        : aio().blockOn((*inputNode)->lockedRef.input.fetch(store)).first.storePath;
                    if (json) {
                        auto& jsonObj3 = jsonObj2[inputName];
                        jsonObj3["path"] = store->printStorePath(storePath);
                        sources.insert(std::move(storePath));
                        jsonObj3["inputs"] = traverse(**inputNode);
                    } else {
                        sources.insert(std::move(storePath));
                        traverse(**inputNode);
                    }
                }
            }
            return jsonObj2;
        };

        if (json) {
            JSON jsonRoot = {
                {"path", store->printStorePath(flake.flake.sourceInfo->storePath)},
                {"inputs", traverse(*flake.lockFile.root)},
            };
            logger->cout("%s", jsonRoot);
        } else {
            traverse(*flake.lockFile.root);
        }

        if (!dryRun && !dstUri.empty()) {
            ref<Store> dstStore = aio().blockOn(dstUri.empty() ? openStore() : openStore(dstUri));
            aio().blockOn(copyPaths(*store, *dstStore, sources));
        }
    }
};

struct CmdFlakeShow : FlakeCommand, MixJSON
{
    bool showLegacy = false;
    bool showAllSystems = false;

    CmdFlakeShow()
    {
        addFlag({
            .longName = "legacy",
            .description = "Show the contents of the `legacyPackages` output.",
            .handler = {&showLegacy, true}
        });
        addFlag({
            .longName = "all-systems",
            .description = "Show the contents of outputs for all systems.",
            .handler = {&showAllSystems, true}
        });
    }

    std::string description() override
    {
        return "show the outputs provided by a flake";
    }

    std::string doc() override
    {
        return
          #include "flake-show.md"
          ;
    }

    void run(nix::ref<nix::Store> store) override
    {
        evalSettings.enableImportFromDerivation.setDefault(false);

        auto evaluator = getEvaluator();
        auto state = evaluator->begin(aio());
        auto flake = std::make_shared<LockedFlake>(lockFlake(*state));
        auto localSystem = std::string(evalSettings.getCurrentSystem());

        std::function<bool(
            eval_cache::AttrCursor & visitor,
            std::vector<std::string> attrPath,
            const std::string &attr)> hasContent;

        // For frameworks it's important that structures are as lazy as possible
        // to prevent infinite recursions, performance issues and errors that
        // aren't related to the thing to evaluate. As a consequence, they have
        // to emit more attributes than strictly (sic) necessary.
        // However, these attributes with empty values are not useful to the user
        // so we omit them.
        hasContent = [&](
            eval_cache::AttrCursor & visitor,
            std::vector<std::string> attrPath,
            const std::string &attr) -> bool
        {
            attrPath.push_back(attr);

            auto visitor2 = visitor.getAttr(*state, attr);

            try {
                if ((attrPath[0] == "apps"
                        || attrPath[0] == "checks"
                        || attrPath[0] == "devShells"
                        || attrPath[0] == "legacyPackages"
                        || attrPath[0] == "packages")
                    && (attrPath.size() == 1 || attrPath.size() == 2)) {
                    for (const auto &subAttr : visitor2->getAttrs(*state)) {
                        if (hasContent(*visitor2, attrPath, subAttr)) {
                            return true;
                        }
                    }
                    return false;
                }

                if ((attrPath.size() == 1)
                    && (attrPath[0] == "formatter"
                        || attrPath[0] == "nixosConfigurations"
                        || attrPath[0] == "nixosModules"
                        || attrPath[0] == "overlays"
                        )) {
                    for (const auto &subAttr : visitor2->getAttrs(*state)) {
                        if (hasContent(*visitor2, attrPath, subAttr)) {
                            return true;
                        }
                    }
                    return false;
                }

                // If we don't recognize it, it's probably content
                return true;
            } catch (EvalError & e) {
                // Some attrs may contain errors, eg. legacyPackages of
                // nixpkgs. We still want to recurse into it, instead of
                // skipping it at all.
                return true;
            }
        };

        std::function<JSON(
            eval_cache::AttrCursor & visitor,
            const std::vector<std::string> & attrPath,
            const std::string & headerPrefix,
            const std::string & nextPrefix,
            NeverAsync)> visit;

        visit = [&](
            eval_cache::AttrCursor & visitor,
            const std::vector<std::string> & attrPath,
            const std::string & headerPrefix,
            const std::string & nextPrefix,
            NeverAsync)
            -> JSON
        {
            auto j = JSON::object();

            Activity act(*logger, lvlInfo, actUnknown,
                fmt("evaluating '%s'", concatStringsSep(".", attrPath)));

            try {
                auto recurse = [&](NeverAsync = {})
                {
                    if (!json)
                        logger->cout("%s", headerPrefix);
                    std::vector<std::string> attrs;
                    for (const auto &attr : visitor.getAttrs(*state)) {
                        if (hasContent(visitor, attrPath, attr))
                            attrs.push_back(attr);
                    }

                    for (const auto & [i, attr] : enumerate(attrs)) {
                        bool last = i + 1 == attrs.size();
                        auto visitor2 = visitor.getAttr(*state, attr);
                        auto attrPath2(attrPath);
                        attrPath2.push_back(attr);
                        auto j2 = visit(*visitor2, attrPath2,
                            fmt(ANSI_GREEN "%s%s" ANSI_NORMAL ANSI_BOLD "%s" ANSI_NORMAL, nextPrefix, last ? treeLast : treeConn, attr),
                            nextPrefix + (last ? treeNull : treeLine), {});
                        if (json) j.emplace(attr, std::move(j2));
                    }
                };

                auto showDerivation = [&](NeverAsync = {})
                {
                    auto name = visitor.getAttr(*state, "name")->getString(*state);
                    std::optional<std::string> description;
                    if (auto aMeta = visitor.maybeGetAttr(*state, "meta")) {
                        if (auto aDescription = aMeta->maybeGetAttr(*state, "description"))
                            description = aDescription->getString(*state);
                    }

                    if (json) {
                        j.emplace("type", "derivation");
                        j.emplace("name", name);
                        if (description)
                            j.emplace("description", *description);
                    } else {
                        auto type =
                            attrPath.size() == 2 && attrPath[0] == "devShell" ? "development environment" :
                            attrPath.size() >= 2 && attrPath[0] == "devShells" ? "development environment" :
                            attrPath.size() == 3 && attrPath[0] == "checks" ? "derivation" :
                            attrPath.size() >= 1 && attrPath[0] == "hydraJobs" ? "derivation" :
                            "package";

                        std::string output = fmt("%s: %s '%s'", headerPrefix, type, name);

                        if (description && !description->empty()) {
                            // Trim the string and only display the first line of the description.
                            auto desc = nix::trim(*description);
                            auto firstLineDesc = desc.substr(0, desc.find('\n'));
                            // three separators, two quotes
                            constexpr auto quotesAndSepsWidth = 3 + 2;

                            int screenWidth = isOutputARealTerminal(StandardOutputStream::Stdout)
                                ? getWindowSize().second
                                : std::numeric_limits<int>::max();

                            // FIXME: handle utf8 visible width properly once we get KJ which has utf8 support
                            //        technically filterANSIEscapes knows how to do this but there is absolutely
                            //        no clear usage of it that would actually let us do this layout.
                            assert(output.size() < std::numeric_limits<int>::max());
                            int spaceForDescription = screenWidth - int(output.size()) - quotesAndSepsWidth;

                            if (spaceForDescription <= 0) {
                                // do nothing, it is going to wrap no matter what, and it's better to output *something*
                            } else {
                                const char *ellipsis = "";
                                assert(firstLineDesc.size() < std::numeric_limits<int>::max());
                                if (spaceForDescription < int(firstLineDesc.size())) {
                                    // subtract one to make space for the ellipsis
                                    firstLineDesc.resize(spaceForDescription - 1);
                                    ellipsis = "…";
                                }
                                output.append(fmt(" - '%s%s'", firstLineDesc, ellipsis));
                            }
                        }
                        logger->cout("%s", output);
                    }
                };

                if (attrPath.size() == 0
                    || (attrPath.size() == 1 && (
                            attrPath[0] == "defaultPackage"
                            || attrPath[0] == "devShell"
                            || attrPath[0] == "formatter"
                            || attrPath[0] == "nixosConfigurations"
                            || attrPath[0] == "nixosModules"
                            || attrPath[0] == "defaultApp"
                            || attrPath[0] == "templates"
                            || attrPath[0] == "overlays"))
                    || ((attrPath.size() == 1 || attrPath.size() == 2)
                        && (attrPath[0] == "checks"
                            || attrPath[0] == "packages"
                            || attrPath[0] == "devShells"
                            || attrPath[0] == "apps"))
                    )
                {
                    recurse();
                }

                else if (
                    (attrPath.size() == 2 && (attrPath[0] == "defaultPackage" || attrPath[0] == "devShell" || attrPath[0] == "formatter"))
                    || (attrPath.size() == 3 && (attrPath[0] == "checks" || attrPath[0] == "packages" || attrPath[0] == "devShells"))
                    )
                {
                    if (!showAllSystems && attrPath[1] != localSystem) {
                        if (!json)
                            logger->cout(fmt("%s " ANSI_WARNING "omitted" ANSI_NORMAL " (use '--all-systems' to show)", headerPrefix));
                        else {
                            printTaggedWarning(
                                "%s omitted (use '--all-systems' to show)",
                                concatStringsSep(".", attrPath)
                            );
                        }
                    } else {
                        if (visitor.isDerivation(*state))
                            showDerivation();
                        else
                            throw Error("expected a derivation");
                    }
                }

                else if (attrPath.size() > 0 && attrPath[0] == "hydraJobs") {
                    if (visitor.isDerivation(*state))
                        showDerivation();
                    else
                        recurse();
                }

                else if (attrPath.size() > 0 && attrPath[0] == "legacyPackages") {
                    if (attrPath.size() == 1)
                        recurse();
                    else if (!showLegacy){
                        if (!json)
                            logger->cout(fmt("%s " ANSI_WARNING "omitted" ANSI_NORMAL " (use '--legacy' to show)", headerPrefix));
                        else {
                            printTaggedWarning(
                                "%s omitted (use '--legacy' to show)",
                                concatStringsSep(".", attrPath)
                            );
                        }
                    } else if (!showAllSystems && attrPath[1] != localSystem) {
                        if (!json)
                            logger->cout(fmt("%s " ANSI_WARNING "omitted" ANSI_NORMAL " (use '--all-systems' to show)", headerPrefix));
                        else {
                            printTaggedWarning(
                                "%s omitted (use '--all-systems' to show)",
                                concatStringsSep(".", attrPath)
                            );
                        }
                    } else {
                        if (visitor.isDerivation(*state))
                            showDerivation();
                        else if (attrPath.size() <= 2)
                            // FIXME: handle recurseIntoAttrs
                            recurse();
                    }
                }

                else if (
                    (attrPath.size() == 2 && attrPath[0] == "defaultApp") ||
                    (attrPath.size() == 3 && attrPath[0] == "apps"))
                {
                    auto aType = visitor.maybeGetAttr(*state, "type");
                    if (!aType || aType->getString(*state) != "app")
                        evaluator->errors.make<EvalError>("not an app definition").debugThrow();
                    if (json) {
                        j.emplace("type", "app");
                    } else {
                        logger->cout("%s: app", headerPrefix);
                    }
                }

                else if (
                    (attrPath.size() == 1 && attrPath[0] == "defaultTemplate") ||
                    (attrPath.size() == 2 && attrPath[0] == "templates"))
                {
                    auto description = visitor.getAttr(*state, "description")->getString(*state);
                    if (json) {
                        j.emplace("type", "template");
                        j.emplace("description", description);
                    } else {
                        logger->cout("%s: template: " ANSI_BOLD "%s" ANSI_NORMAL, headerPrefix, description);
                    }
                }

                else {
                    auto [type, description] =
                        (attrPath.size() == 1 && attrPath[0] == "overlay")
                        || (attrPath.size() == 2 && attrPath[0] == "overlays") ? std::make_pair("nixpkgs-overlay", "Nixpkgs overlay") :
                        attrPath.size() == 2 && attrPath[0] == "nixosConfigurations" ? std::make_pair("nixos-configuration", "NixOS configuration") :
                        (attrPath.size() == 1 && attrPath[0] == "nixosModule")
                        || (attrPath.size() == 2 && attrPath[0] == "nixosModules") ? std::make_pair("nixos-module", "NixOS module") :
                        std::make_pair("unknown", "unknown");
                    if (json) {
                        j.emplace("type", type);
                    } else {
                        logger->cout("%s: " ANSI_WARNING "%s" ANSI_NORMAL, headerPrefix, description);
                    }
                }
            } catch (EvalError & e) {
                if (!(attrPath.size() > 0 && attrPath[0] == "legacyPackages"))
                    throw;
            }

            return j;
        };

        auto cache = openEvalCache(*evaluator, flake);

        auto j = visit(*cache->getRoot(), {}, fmt(ANSI_BOLD "%s" ANSI_NORMAL, flake->flake.lockedRef), "", {});
        if (json)
            logger->cout("%s", j.dump());
    }
};

struct CmdFlakePrefetch : FlakeCommand, MixJSON
{
    CmdFlakePrefetch()
    {
    }

    std::string description() override
    {
        return "download the source tree denoted by a flake reference into the Nix store";
    }

    std::string doc() override
    {
        return
          #include "flake-prefetch.md"
          ;
    }

    void run(ref<Store> store) override
    {
        auto originalRef = getFlakeRef();
        auto resolvedRef = aio().blockOn(originalRef.resolve(store));
        auto [tree, lockedRef] = aio().blockOn(resolvedRef.fetchTree(store));
        auto hash = aio().blockOn(store->queryPathInfo(tree.storePath))->narHash;

        if (json) {
            auto res = JSON::object();
            res["storePath"] = store->printStorePath(tree.storePath);
            res["hash"] = hash.to_string(Base::SRI, true);
            logger->cout(res.dump());
        } else {
            notice("Downloaded '%s' to '%s' (hash '%s').",
                lockedRef.to_string(),
                store->printStorePath(tree.storePath),
                hash.to_string(Base::SRI, true));
        }
    }
};

struct CmdFlake : MultiCommand
{
    CmdFlake()
        : MultiCommand({
                {"update", [](auto & aio) { return make_ref<MixAio<CmdFlakeUpdate>>(aio); }},
                {"lock", [](auto & aio) { return make_ref<MixAio<CmdFlakeLock>>(aio); }},
                {"metadata", [](auto & aio) { return make_ref<MixAio<CmdFlakeMetadata>>(aio); }},
                {"info", [](auto & aio) { return make_ref<MixAio<CmdFlakeInfo>>(aio); }},
                {"check", [](auto & aio) { return make_ref<MixAio<CmdFlakeCheck>>(aio); }},
                {"init", [](auto & aio) { return make_ref<MixAio<CmdFlakeInit>>(aio); }},
                {"new", [](auto & aio) { return make_ref<MixAio<CmdFlakeNew>>(aio); }},
                {"clone", [](auto & aio) { return make_ref<MixAio<CmdFlakeClone>>(aio); }},
                {"archive", [](auto & aio) { return make_ref<MixAio<CmdFlakeArchive>>(aio); }},
                {"show", [](auto & aio) { return make_ref<MixAio<CmdFlakeShow>>(aio); }},
                {"prefetch", [](auto & aio) { return make_ref<MixAio<CmdFlakePrefetch>>(aio); }},
            })
    {
    }

    std::string description() override
    {
        return "manage Nix flakes";
    }

    std::string doc() override
    {
        return
          #include "flake.md"
          ;
    }

    void run() override
    {
        if (!command)
            throw UsageError("'nix flake' requires a sub-command.");
        experimentalFeatureSettings.require(Xp::Flakes);
        command->second->run();
    }
};

void registerNixFlake()
{
    registerCommand<CmdFlake>("flake");
}

}
