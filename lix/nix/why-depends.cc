#include "lix/libcmd/command.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libmain/shared.hh"
#include "lix/libstore/path-tree.hh"
#include "why-depends.hh"

namespace nix {
struct CmdWhyDepends : SourceExprCommand, MixOperateOnOptions
{
    std::string _package, _dependency;
    bool all = false;
    bool precise = false;

    CmdWhyDepends()
    {
        expectArgs({
            .label = "package",
            .handler = {&_package},
            .completer = getCompleteInstallable(),
        });

        expectArgs({
            .label = "dependency",
            .handler = {&_dependency},
            .completer = getCompleteInstallable(),
        });

        addFlag({
            .longName = "all",
            .shortName = 'a',
            .description = "Show all edges in the dependency graph leading from *package* to *dependency*, rather than just a shortest path.",
            .handler = {&all, true},
        });

        addFlag({
            .longName = "precise",
            .description = "For each edge in the dependency graph, show the files in the parent that cause the dependency.",
            .handler = {&precise, true},
        });
    }

    std::string description() override
    {
        return "show why a package has another package in its closure";
    }

    std::string doc() override
    {
        return
          #include "why-depends.md"
          ;
    }

    Category category() override { return catSecondary; }

    void run(ref<Store> store) override
    {
        auto state = getEvaluator()->begin(aio());
        auto package = parseInstallable(*state, store, _package);
        auto packagePath = Installable::toStorePath(*state, getEvalStore(), store, Realise::Outputs, operateOn, package);

        /* We don't need to build `dependency`. We try to get the store
         * path if it's already known, and if not, then it's not a dependency.
         *
         * Why? If `package` does depends on `dependency`, then getting the
         * store path of `package` above necessitated having the store path
         * of `dependency`. The contrapositive is, if the store path of
         * `dependency` is not already known at this point (i.e. it's a CA
         * derivation which hasn't been built), then `package` did not need it
         * to build.
         */
        auto dependency = parseInstallable(*state, store, _dependency);
        auto optDependencyPath = [&]() -> std::optional<StorePath> {
            try {
                return {Installable::toStorePath(*state, getEvalStore(), store, Realise::Derivation, operateOn, dependency)};
            } catch (MissingRealisation &) {
                return std::nullopt;
            }
        }();

        StorePathSet closure;
        aio().blockOn(store->computeFSClosure({packagePath}, closure, false, false));

        if (!optDependencyPath.has_value() || !closure.count(*optDependencyPath)) {
            printError("'%s' does not depend on '%s'", package->what(), dependency->what());
            return;
        }

        auto dependencyPath = *optDependencyPath;

        logger->pause(); // FIXME

        auto accessor = store->getFSAccessor();

        std::map<StorePath, StorePathSet> graphData;
        for (auto & path : closure) {
            graphData.emplace(path, aio().blockOn(store->queryPathInfo(path))->references);
        }

        /* Print the subgraph of nodes that have 'dependency' in their
           closure (i.e., that have a non-infinite distance to
           'dependency'). Print every edge on a path between `package`
           and `dependency`. */
        RunPager pager;
        logger->cout(
            "%s",
            aio().blockOn(
                genGraphString(packagePath, dependencyPath, graphData, *store, all, precise)
            )
        );
    }
};

void registerNixWhyDepends()
{
    registerCommand<CmdWhyDepends>("why-depends");
}

}
