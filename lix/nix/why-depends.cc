#include "lix/libcmd/command.hh"
#include "lix/libstore/store-api.hh"
#include "lix/libstore/fs-accessor.hh"
#include "lix/libmain/shared.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/generator.hh"
#include "lix/libutil/result.hh"
#include "why-depends.hh"

#include <queue>

namespace nix {

static std::string hilite(const std::string & s, size_t pos, size_t len,
    const std::string & colour = ANSI_RED)
{
    return
        std::string(s, 0, pos)
        + colour
        + std::string(s, pos, len)
        + ANSI_NORMAL
        + std::string(s, pos + len);
}

static std::string filterPrintable(const std::string & s)
{
    std::string res;
    for (char c : s)
        res += isprint(c) ? c : '.';
    return res;
}

static kj::Promise<Result<std::map<std::string, Strings>>> visitPath(
    const Path & p,
    Store & store,
    const Path & pathS,
    std::string_view dependencyPathHash,
    const std::set<std::string> & hashes,
    const StorePath & from,
    const StorePath & to
)
try {
    /* For each reference, find the files and symlinks that
       contain the reference. */
    std::map<std::string, Strings> hits;
    auto accessor = store.getFSAccessor();
    auto st = TRY_AWAIT(accessor->stat(p));

    auto p2 = p == pathS ? "/" : std::string(p, pathS.size() + 1);

    auto getColour = [&](const std::string & hash) {
        return hash == dependencyPathHash ? ANSI_GREEN : ANSI_BLUE;
    };

    if (st.type == FSAccessor::Type::tDirectory) {
        auto names = TRY_AWAIT(accessor->readDirectory(p));
        for (auto & name : names) {
            auto found = TRY_AWAIT(
                visitPath(p + "/" + name, store, pathS, dependencyPathHash, hashes, from, to)
            );
            hits.merge(found);
        }
    } else if (st.type == FSAccessor::Type::tRegular) {
        auto contents = TRY_AWAIT(accessor->readFile(p));

        for (auto & hash : hashes) {
            auto pos = contents.find(hash);
            if (pos != std::string::npos) {
                size_t margin = 32;
                auto pos2 = pos >= margin ? pos - margin : 0;
                hits[hash].emplace_back(
                    fmt("%s: …%s…",
                        p2,
                        hilite(
                            filterPrintable(
                                std::string(contents, pos2, pos - pos2 + hash.size() + margin)
                            ),
                            pos - pos2,
                            StorePath::HashLen,
                            getColour(hash)
                        ))
                );
            }
        }
    } else if (st.type == FSAccessor::Type::tSymlink) {
        auto target = TRY_AWAIT(accessor->readLink(p));

        for (auto & hash : hashes) {
            auto pos = target.find(hash);
            if (pos != std::string::npos) {
                hits[hash].emplace_back(
                    fmt("%s -> %s", p2, hilite(target, pos, StorePath::HashLen, getColour(hash)))
                );
            }
        }
    }

    co_return hits;
} catch (...) {
    co_return result::current_exception();
}

struct Node
{
    StorePath path;
    StorePathSet dependencies;
    StorePathSet dependents;
    std::optional<size_t> dist = std::nullopt;
    Node * prev = nullptr;
    bool queued = false;
    bool visited = false;
};

struct BailOut : BaseException
{};

static kj::Promise<Result<void>> printNode(
    Node & node,
    const std::string & firstPad,
    const std::string & tailPad,
    bool all,
    bool precise,
    Store & store,
    const StorePath & packagePath,
    const StorePath & dependencyPath,
    std::map<StorePath, Node> & graph,
    Strings & output
)
try {
    auto pathS = store.printStorePath(node.path);

    assert(node.dist.has_value());
    if (precise) {
        output.push_back(
            fmt("%s%s%s%s" ANSI_NORMAL,
                firstPad,
                node.visited ? "\e[38;5;244m" : "",
                firstPad != "" ? "→ " : "",
                pathS)
        );
    }

    if (node.path == dependencyPath && !all && packagePath != dependencyPath) {
        throw BailOut();
    }

    if (node.visited) {
        co_return result::success();
    }

    if (precise) {
        node.visited = true;
    }

    /* Sort the references by distance to `dependency` to
       ensure that the shortest path is printed first. */
    std::multimap<size_t, Node *> refs;
    std::set<std::string> hashes;

    for (auto & ref : node.dependencies) {
        if (ref == node.path && packagePath != dependencyPath) {
            continue;
        }
        auto & node2 = graph.at(ref);
        if (auto dist = node2.dist) {
            refs.emplace(*node2.dist, &node2);
            hashes.insert(std::string(node2.path.hashPart()));
        }
    }

    /* For each reference, find the files and symlinks that
       contain the reference. */
    std::map<std::string, Strings> hits;

    // FIXME: should use scanForReferences().

    if (precise) {
        hits = TRY_AWAIT(visitPath(
            pathS, store, pathS, dependencyPath.hashPart(), hashes, packagePath, dependencyPath
        ));
    }

    for (auto & ref : refs) {
        std::string hash(ref.second->path.hashPart());

        bool last = all ? ref == *refs.rbegin() : true;

        for (auto & hit : hits[hash]) {
            bool first = hit == *hits[hash].begin();
            output.push_back(
                fmt("%s%s%s",
                    tailPad,
                    (first ? (last ? treeLast : treeConn) : (last ? treeNull : treeLine)),
                    hit)
            );
            if (!all) {
                break;
            }
        }

        if (!precise) {
            auto pathS = store.printStorePath(ref.second->path);
            output.push_back(
                fmt("%s%s%s%s" ANSI_NORMAL,
                    firstPad,
                    ref.second->visited ? "\e[38;5;244m" : "",
                    last ? treeLast : treeConn,
                    pathS)
            );
            node.visited = true;
        }

        TRY_AWAIT(printNode(
            *ref.second,
            tailPad + (last ? treeNull : treeLine),
            tailPad + (last ? treeNull : treeLine),
            all,
            precise,
            store,
            packagePath,
            dependencyPath,
            graph,
            output
        ));
    }

    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

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

        std::map<StorePath, Node> graph;

        for (auto & path : closure)
            graph.emplace(
                path,
                Node{
                    .path = path,
                    .dependencies = aio().blockOn(store->queryPathInfo(path))->references,
                    .dist = path == dependencyPath ? std::optional(0) : std::nullopt
                }
            );

        // Transpose the graph.
        for (auto & node : graph)
            for (auto & ref : node.second.dependencies) {
                graph.find(ref)->second.dependents.insert(node.first);
            }

        /* Run Dijkstra's shortest path algorithm to get the distance
           of every path in the closure to 'dependency'. */
        std::priority_queue<Node *> queue;

        queue.push(&graph.at(dependencyPath));
        auto const inf = std::numeric_limits<size_t>::max();

        while (!queue.empty()) {
            auto & node = *queue.top();
            queue.pop();

            for (auto & rref : node.dependents) {
                auto & node2 = graph.at(rref);
                auto dist = node.dist.transform([](auto n) { return n + 1; });
                if (dist.value_or(inf) < node2.dist.value_or(inf)) {
                    node2.dist = dist;
                    node2.prev = &node;
                    if (!node2.queued) {
                        node2.queued = true;
                        queue.push(&node2);
                    }
                }
            }
        }

        /* Print the subgraph of nodes that have 'dependency' in their
           closure (i.e., that have a non-infinite distance to
           'dependency'). Print every edge on a path between `package`
           and `dependency`. */
        RunPager pager;
        if (!precise) {
            logger->cout("%s", store->printStorePath(graph.at(packagePath).path));
        }

        Strings output;
        try {
            aio().blockOn(printNode(
                graph.at(packagePath),
                "",
                "",
                all,
                precise,
                *store,
                packagePath,
                dependencyPath,
                graph,
                output
            ));
        } catch (BailOut &) {
        }

        for (auto & l : output) {
            logger->cout("%s", l);
        }
    }
};

void registerNixWhyDepends()
{
    registerCommand<CmdWhyDepends>("why-depends");
}

}
