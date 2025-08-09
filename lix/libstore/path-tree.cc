#include "path-tree.hh"
#include "fs-accessor.hh"
#include "lix/libutil/ansicolor.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/strings.hh"
#include "lix/libutil/fmt.hh"
#include <boost/algorithm/string/join.hpp>
#include <queue>

#define ANSI_DIM_ALREADY_VISITED "\e[38;5;244m"

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
    const StorePath & to,
    const ref<FSAccessor> & accessor
)
try {
    /* For each reference, find the files and symlinks that
       contain the reference. */
    std::map<std::string, Strings> hits;
    auto st = TRY_AWAIT(accessor->stat(p));

    auto p2 = p == pathS ? "/" : std::string(p, pathS.size() + 1);

    auto getColour = [&](const std::string & hash) {
        return hash == dependencyPathHash ? ANSI_GREEN : ANSI_BLUE;
    };

    if (st.type == FSAccessor::Type::tDirectory) {
        auto names = TRY_AWAIT(accessor->readDirectory(p));
        for (auto & name : names) {
            auto found = TRY_AWAIT(visitPath(
                p + "/" + name, store, pathS, dependencyPathHash, hashes, from, to, accessor
            ));
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
                            StorePath::HASH_PART_LEN,
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
                hits[hash].emplace_back(fmt(
                    "%s -> %s", p2, hilite(target, pos, StorePath::HASH_PART_LEN, getColour(hash))
                ));
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
    Strings & output,
    ref<FSAccessor> accessor
)
try {
    auto pathS = store.printStorePath(node.path);

    assert(node.dist.has_value());
    if (precise) {
        output.push_back(
            fmt("%s%s%s%s" ANSI_NORMAL,
                firstPad,
                node.path == dependencyPath ? ANSI_NORMAL
                    : node.visited          ? ANSI_DIM_ALREADY_VISITED
                                            : "",
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
            pathS,
            store,
            pathS,
            dependencyPath.hashPart(),
            hashes,
            packagePath,
            dependencyPath,
            accessor
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
                    ref.second->path == dependencyPath ? ANSI_BOLD
                        : ref.second->visited          ? ANSI_DIM_ALREADY_VISITED
                                                       : "",
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
            output,
            accessor
        ));
    }

    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

static std::map<StorePath, Node> mkGraph(
    const StorePath & packagePath,
    const StorePath & dependencyPath,
    const std::map<StorePath, StorePathSet> & graph,
    Store & store,
    bool all,
    bool precise
)
{
    std::map<StorePath, Node> graph_data;
    for (auto & [path, dependencies] : graph) {
        graph_data.emplace(
            path,
            Node{
                .path = path,
                .dependencies = dependencies,
                .dist = path == dependencyPath ? std::optional(0) : std::nullopt,
            }
        );
    }

    for (auto & node : graph_data) {
        for (auto & ref : node.second.dependencies) {
            graph_data.find(ref)->second.dependents.insert(node.first);
        }
    }

    /* Run Dijkstra's shortest path algorithm to get the distance
       of every path in the closure to 'dependency'. */
    std::priority_queue<Node *> queue;

    queue.push(&graph_data.at(dependencyPath));
    auto const inf = std::numeric_limits<size_t>::max();

    while (!queue.empty()) {
        auto & node = *queue.top();
        queue.pop();

        for (auto & rref : node.dependents) {
            auto & node2 = graph_data.at(rref);
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

    return graph_data;
}

kj::Promise<Result<std::string>> genGraphString(
    const StorePath & start,
    const StorePath & to,
    const std::map<StorePath, StorePathSet> & graphData,
    Store & store,
    bool all,
    bool precise,
    std::optional<ref<FSAccessor>> maybeAccessor
)
try {
    auto graph = mkGraph(start, to, graphData, store, all, precise);
    auto accessor = maybeAccessor ? *maybeAccessor : store.getFSAccessor();

    Strings output;
    if (!precise) {
        output.push_back(fmt("%s", store.printStorePath(graph.at(start).path)));
    }

    try {
        TRY_AWAIT(printNode(
            graph.at(start), "", "", all, precise, store, start, to, graph, output, accessor
        ));
    } catch (BailOut &) {
    }

    co_return concatStringsSep("\n", output);
} catch (...) {
    co_return result::current_exception();
}

}
