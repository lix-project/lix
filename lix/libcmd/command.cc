#include "lix/libcmd/command.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/local-fs-store.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libstore/profiles.hh"
#include "lix/libcmd/repl.hh"
#include "lix/libutil/async.hh"

extern char * * environ __attribute__((weak));

namespace nix {

CommandRegistry::CommandMap * CommandRegistry::commands = nullptr;

nix::CommandMap CommandRegistry::getCommandsFor(const std::vector<std::string> & prefix)
{
    if (!CommandRegistry::commands) {
        CommandRegistry::commands = new CommandMap;
    }
    nix::CommandMap res;
    for (auto & [name, command] : *CommandRegistry::commands) {
        if (name.size() == prefix.size() + 1) {
            bool equal = true;
            for (size_t i = 0; i < prefix.size(); ++i) {
                if (name[i] != prefix[i]) {
                    equal = false;
                }
            }
            if (equal) {
                res.insert_or_assign(name[prefix.size()], command);
            }
        }
    }
    return res;
}

StoreCommand::StoreCommand()
{
}

ref<Store> StoreCommand::getStore()
{
    if (!_store) {
        _store = createStore(aio());
    }
    return *_store;
}

ref<Store> StoreCommand::createStore(AsyncIoRoot & in)
{
    return in.blockOn(openStore());
}

void StoreCommand::run()
{
    run(getStore());
}

CopyCommand::CopyCommand()
{
    addFlag({
        .longName = "from",
        .description = "URL of the source Nix store.",
        .labels = {"store-uri"},
        .handler = {&srcUri},
    });

    addFlag({
        .longName = "to",
        .description = "URL of the destination Nix store.",
        .labels = {"store-uri"},
        .handler = {&dstUri},
    });
}

ref<Store> CopyCommand::createStore(AsyncIoRoot & in)
{
    return srcUri.empty() ? StoreCommand::createStore(in) : in.blockOn(openStore(srcUri));
}

ref<Store> CopyCommand::getDstStore()
{
    if (srcUri.empty() && dstUri.empty())
        throw UsageError("you must pass '--from' and/or '--to'");

    return aio().blockOn(dstUri.empty() ? openStore() : openStore(dstUri));
}

EvalCommand::EvalCommand()
{
    addFlag({
        .longName = "debugger",
        .description = "Start an interactive environment if evaluation fails.",
        .category = MixEvalArgs::category,
        .handler = {&startReplOnEvalErrors, true},
    });
}

EvalCommand::~EvalCommand()
{
    if (evalState)
        evalState->maybePrintStats();
}

ref<Store> EvalCommand::getEvalStore()
{
    if (!evalStore)
        evalStore = evalStoreUrl ? aio().blockOn(openStore(*evalStoreUrl)) : getStore();
    return *evalStore;
}

ref<eval_cache::CachingEvaluator> EvalCommand::getEvaluator()
{
    if (!evalState) {
        evalState = std::allocate_shared<eval_cache::CachingEvaluator>(
            TraceableAllocator<EvalState>(), aio(), searchPath, getEvalStore(), getStore(),
            startReplOnEvalErrors ? AbstractNixRepl::runSimple : nullptr
        );

        evalState->repair = repair;
    }
    return ref<eval_cache::CachingEvaluator>::unsafeFromPtr(evalState);
}

MixOperateOnOptions::MixOperateOnOptions()
{
    addFlag({
        .longName = "derivation",
        .description = "Operate on the [store derivation](../../glossary.md#gloss-store-derivation) rather than its outputs.",
        .category = installablesCategory,
        .handler = {&operateOn, OperateOn::Derivation},
    });
}

BuiltPathsCommand::BuiltPathsCommand(bool recursive)
    : recursive(recursive)
{
    if (recursive)
        addFlag({
            .longName = "no-recursive",
            .description = "Apply operation to specified paths only.",
            .category = installablesCategory,
            .handler = {&this->recursive, false},
        });
    else
        addFlag({
            .longName = "recursive",
            .shortName = 'r',
            .description = "Apply operation to closure of the specified paths.",
            .category = installablesCategory,
            .handler = {&this->recursive, true},
        });

    addFlag({
        .longName = "all",
        .description = "Apply the operation to every store path.",
        .category = installablesCategory,
        .handler = {&all, true},
    });
}

void BuiltPathsCommand::run(ref<Store> store, Installables && installables)
{
    BuiltPaths paths;
    if (all) {
        if (installables.size())
            throw UsageError("'--all' does not expect arguments");
        // XXX: Only uses opaque paths, ignores all the realisations
        for (auto & p : aio().blockOn(store->queryAllValidPaths()))
            paths.emplace_back(BuiltPath::Opaque{p});
    } else {
        paths = Installable::toBuiltPaths(
            *getEvaluator()->begin(aio()),
            getEvalStore(),
            store,
            realiseMode,
            operateOn,
            installables
        );
        if (recursive) {
            // XXX: This only computes the store path closure, ignoring
            // intermediate realisations
            StorePathSet pathsRoots, pathsClosure;
            for (auto & root : paths) {
                auto rootFromThis = root.outPaths();
                pathsRoots.insert(rootFromThis.begin(), rootFromThis.end());
            }
            aio().blockOn(store->computeFSClosure(pathsRoots, pathsClosure));
            for (auto & path : pathsClosure)
                paths.emplace_back(BuiltPath::Opaque{path});
        }
    }

    run(store, std::move(paths));
}

StorePathsCommand::StorePathsCommand(bool recursive)
    : BuiltPathsCommand(recursive)
{
}

void StorePathsCommand::run(ref<Store> store, BuiltPaths && paths)
{
    StorePathSet storePaths;
    for (auto & builtPath : paths)
        for (auto & p : builtPath.outPaths())
            storePaths.insert(p);

    auto sorted = aio().blockOn(store->topoSortPaths(storePaths));
    std::reverse(sorted.begin(), sorted.end());

    run(store, std::move(sorted));
}

void StorePathCommand::run(ref<Store> store, StorePaths && storePaths)
{
    if (storePaths.size() != 1)
        throw UsageError("this command requires exactly one store path");

    run(store, *storePaths.begin());
}

MixProfile::MixProfile()
{
    addFlag({
        .longName = "profile",
        .description = "The profile to operate on.",
        .labels = {"path"},
        .handler = {&profile},
        .completer = completePath
    });
}

void MixProfile::updateProfile(const StorePath & storePath)
{
    if (!profile) return;
    auto store = getStore().try_cast_shared<LocalFSStore>();
    if (!store) throw Error("'--profile' is not supported for this Nix store");
    auto profile2 = absPath(*profile);
    switchLink(profile2,
        aio().blockOn(createGeneration(*store, profile2, storePath)));
}

void MixProfile::updateProfile(const BuiltPaths & buildables)
{
    if (!profile) return;

    StorePaths result;

    for (auto & buildable : buildables) {
        std::visit(overloaded {
            [&](const BuiltPath::Opaque & bo) {
                result.push_back(bo.path);
            },
            [&](const BuiltPath::Built & bfd) {
                for (auto & output : bfd.outputs) {
                    result.push_back(output.second);
                }
            },
        }, buildable.raw());
    }

    if (result.size() != 1)
        throw UsageError("'--profile' requires that the arguments produce a single store path, but there are %d", result.size());

    updateProfile(result[0]);
}

MixDefaultProfile::MixDefaultProfile()
{
    profile = getDefaultProfile();
}

MixEnvironment::MixEnvironment() : ignoreEnvironment(false)
{
    addFlag({
        .longName = "ignore-environment",
        .shortName = 'i',
        .description = "Clear the entire environment (except those specified with `--keep`).",
        .handler = {&ignoreEnvironment, true},
    });

    addFlag({
        .longName = "keep",
        .shortName = 'k',
        .description = "Keep the environment variable *name*.",
        .labels = {"name"},
        .handler = {[&](std::string s) { keep.insert(s); }},
    });

    addFlag({
        .longName = "unset",
        .shortName = 'u',
        .description = "Unset the environment variable *name*.",
        .labels = {"name"},
        .handler = {[&](std::string s) { unset.insert(s); }},
    });
}

void MixEnvironment::setEnviron() {
    if (ignoreEnvironment) {
        if (!unset.empty())
            throw UsageError("--unset does not make sense with --ignore-environment");

        for (const auto & var : keep) {
            auto val = getenv(var.c_str());
            if (val) stringsEnv.emplace_back(fmt("%s=%s", var.c_str(), val));
        }

        vectorEnv = stringsToCharPtrs(stringsEnv);
        environ = vectorEnv.data();
    } else {
        if (!keep.empty())
            throw UsageError("--keep does not make sense without --ignore-environment");

        for (const auto & var : unset)
            unsetenv(var.c_str());
    }
}

}
