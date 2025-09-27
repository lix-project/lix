#include "user-env.hh"
#include "lix/libexpr/value.hh"
#include "lix/libstore/derivations.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/path-with-outputs.hh"
#include "lix/libstore/local-fs-store.hh"
#include "lix/libmain/shared.hh"
#include "lix/libexpr/eval.hh"
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
        if (auto drvPath = i.queryDrvPath(state))
            drvsToBuild.push_back({*drvPath});

    debug("building user environment dependencies");
    state.aio.blockOn(state.ctx.store->buildPaths(
        toDerivedPaths(drvsToBuild),
        state.ctx.repair ? bmRepair : bmNormal));

    /* Construct the whole top level derivation. */
    StorePathSet references;
    auto manifest = state.ctx.mem.newList(elems.size());
    Value vManifest{NewValueAs::list, manifest};
    size_t n = 0;
    for (auto & i : elems) {
        /* Create a pseudo-derivation containing the name, system,
           output paths, and optionally the derivation path, as well
           as the meta attributes. */
        std::optional<StorePath> drvPath = keepDerivations ? i.queryDrvPath(state) : std::nullopt;
        DrvInfo::Outputs outputs = i.queryOutputs(state, true, true);
        StringSet metaNames = i.queryMetaNames(state);

        auto attrs = state.ctx.buildBindings(7 + outputs.size());

        attrs.alloc(state.ctx.s.type).mkString("derivation");
        attrs.alloc(state.ctx.s.name).mkString(i.queryName(state));
        auto system = i.querySystem(state);
        if (!system.empty())
            attrs.alloc(state.ctx.s.system).mkString(system);
        attrs.alloc(state.ctx.s.outPath).mkString(state.ctx.store->printStorePath(i.queryOutPath(state)));
        if (drvPath)
            attrs.alloc(state.ctx.s.drvPath).mkString(state.ctx.store->printStorePath(*drvPath));

        // Copy each output meant for installation.
        auto & vOutputs = attrs.alloc(state.ctx.s.outputs);
        auto outputsList = state.ctx.mem.newList(outputs.size());
        vOutputs = {NewValueAs::list, outputsList};
        for (const auto & [m, j] : enumerate(outputs)) {
            outputsList->elems[m].mkString(j.first);
            auto outputAttrs = state.ctx.buildBindings(2);
            outputAttrs.alloc(state.ctx.s.outPath).mkString(state.ctx.store->printStorePath(*j.second));
            attrs.alloc(j.first).mkAttrs(outputAttrs);

            /* This is only necessary when installing store paths, e.g.,
               `nix-env -i /nix/store/abcd...-foo'. */
            state.aio.blockOn(state.ctx.store->addTempRoot(*j.second));
            state.aio.blockOn(state.ctx.store->ensurePath(*j.second));

            references.insert(*j.second);
        }

        // Copy the meta attributes.
        auto meta = state.ctx.buildBindings(metaNames.size());
        for (auto & j : metaNames) {
            Value * v = i.queryMeta(state, j);
            if (!v) continue;
            meta.insert(state.ctx.symbols.create(j), *v);
        }

        attrs.alloc(state.ctx.s.meta).mkAttrs(meta);

        manifest->elems[n++].mkAttrs(attrs);

        if (drvPath) references.insert(*drvPath);
    }

    /* Also write a copy of the list of user environment elements to
       the store; we need it for future modifications of the
       environment. */
    std::ostringstream str;
    printAmbiguous(vManifest, state.ctx.symbols, str, nullptr, std::numeric_limits<int>::max());
    auto manifestFile = state.aio.blockOn(state.ctx.store->addTextToStore("env-manifest.nix",
        str.str(), references));

    /* Get the environment builder expression. */
    Value envBuilder;
    state.eval(state.ctx.parseExprFromString(
        #include "buildenv.nix.gen.hh"
            , CanonPath::root), envBuilder);

    /* Construct a Nix expression that calls the user environment
       builder with the manifest as argument. */
    auto attrs = state.ctx.buildBindings(3);
    state.ctx.paths.mkStorePathString(manifestFile, attrs.alloc("manifest"));
    attrs.insert(state.ctx.symbols.create("derivations"), vManifest);
    Value args;
    args.mkAttrs(attrs);

    Value topLevel{NewValueAs::app, state.ctx.mem, envBuilder, args};

    /* Evaluate it. */
    debug("evaluating user environment builder");
    state.forceValue(topLevel, noPos);
    NixStringContext context;
    const Attr & aDrvPath(*topLevel.attrs()->get(state.ctx.s.drvPath));
    auto topLevelDrv = state.coerceToStorePath(aDrvPath.pos, aDrvPath.value, context, "");
    const Attr & aOutPath(*topLevel.attrs()->get(state.ctx.s.outPath));
    auto topLevelOut = state.coerceToStorePath(aOutPath.pos, aOutPath.value, context, "");

    /* Realise the resulting store expression. */
    debug("building user environment");
    std::vector<StorePathWithOutputs> topLevelDrvs;
    topLevelDrvs.push_back({topLevelDrv});
    state.aio.blockOn(state.ctx.store->buildPaths(
        toDerivedPaths(topLevelDrvs),
        state.ctx.repair ? bmRepair : bmNormal));

    /* Switch the current user environment to the output path. */
    auto store2 = state.ctx.store.try_cast_shared<LocalFSStore>();

    if (store2) {
        PathLock lock = lockProfile(profile);

        Path lockTokenCur = optimisticLockProfile(profile);
        if (lockToken != lockTokenCur) {
            printInfo("profile '%1%' changed while we were busy; restarting", profile);
            return false;
        }

        debug("switching to new user environment");
        Path generation = state.aio.blockOn(createGeneration(*store2, profile, topLevelOut));
        switchLink(profile, generation);
    }

    return true;
}


}
