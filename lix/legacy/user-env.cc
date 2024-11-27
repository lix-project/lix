#include "user-env.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/path-with-outputs.hh"
#include "lix/libstore/local-fs-store.hh"
#include "lix/libstore/globals.hh"
#include "lix/libmain/shared.hh"
#include "lix/libexpr/eval.hh"
#include "lix/libexpr/eval-inline.hh"
#include "lix/libstore/profiles.hh"
#include "lix/libexpr/print-ambiguous.hh"

#include <limits>
#include <sstream>

namespace nix {


bool createUserEnv(EvalState & state, DrvInfos & elems,
    const Path & profile, bool keepDerivations,
    const std::string & lockToken)
{
    /* Build the components in the user environment, if they don't
       exist already. */
    std::vector<StorePathWithOutputs> drvsToBuild;
    for (auto & i : elems)
        if (auto drvPath = i.queryDrvPath())
            drvsToBuild.push_back({*drvPath});

    debug("building user environment dependencies");
    state.store->buildPaths(
        toDerivedPaths(drvsToBuild),
        state.repair ? bmRepair : bmNormal);

    /* Construct the whole top level derivation. */
    StorePathSet references;
    Value manifest;
    state.mkList(manifest, elems.size());
    size_t n = 0;
    for (auto & i : elems) {
        /* Create a pseudo-derivation containing the name, system,
           output paths, and optionally the derivation path, as well
           as the meta attributes. */
        std::optional<StorePath> drvPath = keepDerivations ? i.queryDrvPath() : std::nullopt;
        DrvInfo::Outputs outputs = i.queryOutputs(true, true);
        StringSet metaNames = i.queryMetaNames();

        auto attrs = state.buildBindings(7 + outputs.size());

        attrs.alloc(state.s.type).mkString("derivation");
        attrs.alloc(state.s.name).mkString(i.queryName());
        auto system = i.querySystem();
        if (!system.empty())
            attrs.alloc(state.s.system).mkString(system);
        attrs.alloc(state.s.outPath).mkString(state.store->printStorePath(i.queryOutPath()));
        if (drvPath)
            attrs.alloc(state.s.drvPath).mkString(state.store->printStorePath(*drvPath));

        // Copy each output meant for installation.
        auto & vOutputs = attrs.alloc(state.s.outputs);
        state.mkList(vOutputs, outputs.size());
        for (const auto & [m, j] : enumerate(outputs)) {
            (vOutputs.listElems()[m] = state.allocValue())->mkString(j.first);
            auto outputAttrs = state.buildBindings(2);
            outputAttrs.alloc(state.s.outPath).mkString(state.store->printStorePath(*j.second));
            attrs.alloc(j.first).mkAttrs(outputAttrs);

            /* This is only necessary when installing store paths, e.g.,
               `nix-env -i /nix/store/abcd...-foo'. */
            state.store->addTempRoot(*j.second);
            state.store->ensurePath(*j.second);

            references.insert(*j.second);
        }

        // Copy the meta attributes.
        auto meta = state.buildBindings(metaNames.size());
        for (auto & j : metaNames) {
            Value * v = i.queryMeta(j);
            if (!v) continue;
            meta.insert(state.symbols.create(j), v);
        }

        attrs.alloc(state.s.meta).mkAttrs(meta);

        (manifest.listElems()[n++] = state.allocValue())->mkAttrs(attrs);

        if (drvPath) references.insert(*drvPath);
    }

    /* Also write a copy of the list of user environment elements to
       the store; we need it for future modifications of the
       environment. */
    std::ostringstream str;
    printAmbiguous(manifest, state.symbols, str, nullptr, std::numeric_limits<int>::max());
    auto manifestFile = state.store->addTextToStore("env-manifest.nix",
        str.str(), references);

    /* Get the environment builder expression. */
    Value envBuilder;
    state.eval(state.parseExprFromString(
        #include "buildenv.nix.gen.hh"
            , state.rootPath(CanonPath::root)), envBuilder);

    /* Construct a Nix expression that calls the user environment
       builder with the manifest as argument. */
    auto attrs = state.buildBindings(3);
    state.mkStorePathString(manifestFile, attrs.alloc("manifest"));
    attrs.insert(state.symbols.create("derivations"), &manifest);
    Value args;
    args.mkAttrs(attrs);

    Value topLevel;
    topLevel.mkApp(&envBuilder, &args);

    /* Evaluate it. */
    debug("evaluating user environment builder");
    state.forceValue(topLevel, topLevel.determinePos(noPos));
    NixStringContext context;
    Attr & aDrvPath(*topLevel.attrs->find(state.s.drvPath));
    auto topLevelDrv = state.coerceToStorePath(aDrvPath.pos, *aDrvPath.value, context, "");
    Attr & aOutPath(*topLevel.attrs->find(state.s.outPath));
    auto topLevelOut = state.coerceToStorePath(aOutPath.pos, *aOutPath.value, context, "");

    /* Realise the resulting store expression. */
    debug("building user environment");
    std::vector<StorePathWithOutputs> topLevelDrvs;
    topLevelDrvs.push_back({topLevelDrv});
    state.store->buildPaths(
        toDerivedPaths(topLevelDrvs),
        state.repair ? bmRepair : bmNormal);

    /* Switch the current user environment to the output path. */
    auto store2 = state.store.dynamic_pointer_cast<LocalFSStore>();

    if (store2) {
        PathLocks lock;
        lockProfile(lock, profile);

        Path lockTokenCur = optimisticLockProfile(profile);
        if (lockToken != lockTokenCur) {
            printInfo("profile '%1%' changed while we were busy; restarting", profile);
            return false;
        }

        debug("switching to new user environment");
        Path generation = createGeneration(*store2, profile, topLevelOut);
        switchLink(profile, generation);
    }

    return true;
}


}
