#pragma once
///@file

#include "lix/libutil/async.hh"
#include "lix/libutil/async-collect.hh"
#include "lix/libutil/error.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/sync.hh"

#include <kj/async.h>
#include <map>
#include <queue>
#include <functional>
#include <thread>
#include <atomic>

namespace nix {

MakeError(ThreadPoolShutDown, Error);

/**
 * A simple thread pool that executes a queue of work items
 * (lambdas).
 */
class ThreadPool
{
public:

    ThreadPool(const char * name, size_t maxThreads = 0);

    ~ThreadPool();

    /**
     * An individual work item.
     *
     * \todo use std::packaged_task?
     */
    typedef std::function<void(AsyncIoRoot &)> work_t;

    /**
     * Enqueue a function to be executed by the thread pool.
     */
    void enqueueWithAio(const work_t & t);

    void enqueue(std::function<void()> t)
    {
        enqueueWithAio([t{std::move(t)}](AsyncIoRoot &) { t(); });
    }

    /**
     * Execute work items until the queue is empty.
     *
     * \note Note that work items are allowed to add new items to the
     * queue; this is handled correctly.
     *
     * Queue processing stops prematurely if any work item throws an
     * exception. This exception is propagated to the calling thread. If
     * multiple work items throw an exception concurrently, only one
     * item is propagated; the others are printed on stderr and
     * otherwise ignored.
     */
    void process();

    /** Like `process`, but async. */
    kj::Promise<Result<void>> processAsync();

private:

    size_t maxThreads;

    const char * name;

    struct State
    {
        std::queue<work_t> pending;
        size_t active = 0;
        std::exception_ptr exception;
        std::vector<std::thread> workers;
        bool draining = false;
        std::optional<kj::Own<kj::CrossThreadPromiseFulfiller<void>>> anyWorkerExited;
    };

    std::atomic_bool quit{false};

    Sync<State> state_;

    std::condition_variable work;

    void doWork();

    void shutdown();
};

/**
 * Process in parallel a set of items of type T that have a partial
 * ordering between them. Thus, any item is only processed after all
 * its dependencies have been processed.
 */
template<typename T>
void processGraph(
    const char *poolName,
    const std::set<T> & nodes,
    std::function<std::set<T>(AsyncIoRoot &, const T &)> getEdges,
    std::function<void(AsyncIoRoot &, const T &)> processNode)
{
    struct Graph {
        std::set<T> left;
        std::map<T, std::set<T>> refs, rrefs;
    };

    Sync<Graph> graph_(Graph{nodes, {}, {}});

    std::function<void(AsyncIoRoot &, const T &)> worker;

    /* Create pool last to ensure threads are stopped before other destructors
     * run */
    ThreadPool pool{poolName};


    worker = [&](AsyncIoRoot & aio, const T & node) {

        {
            auto graph(graph_.lock());
            auto i = graph->refs.find(node);
            if (i == graph->refs.end())
                goto getRefs;
            goto doWork;
        }

    getRefs:
        {
            auto refs = getEdges(aio, node);
            refs.erase(node);

            {
                auto graph(graph_.lock());
                for (auto & ref : refs)
                    if (graph->left.count(ref)) {
                        graph->refs[node].insert(ref);
                        graph->rrefs[ref].insert(node);
                    }
                if (graph->refs[node].empty())
                    goto doWork;
            }
        }

        return;

    doWork:
        processNode(aio, node);

        /* Enqueue work for all nodes that were waiting on this one
           and have no unprocessed dependencies. */
        {
            auto graph(graph_.lock());
            for (auto & rref : graph->rrefs[node]) {
                auto & refs(graph->refs[rref]);
                auto i = refs.find(node);
                assert(i != refs.end());
                refs.erase(i);
                if (refs.empty())
                    pool.enqueueWithAio(std::bind(worker, std::placeholders::_1, rref));
            }
            graph->left.erase(node);
            graph->refs.erase(node);
            graph->rrefs.erase(node);
        }
    };

    for (auto & node : nodes)
        pool.enqueueWithAio(std::bind(worker, std::placeholders::_1, std::ref(node)));

    pool.process();

    if (!graph_.lock()->left.empty())
        throw Error("graph processing incomplete (cyclic reference?)");
}

template<typename T>
void processGraph(
    const char *poolName,
    const std::set<T> & nodes,
    std::function<std::set<T>(const T &)> getEdges,
    std::function<void(const T &)> processNode)
{
    processGraph<T>(
        poolName,
        nodes,
        [&](AsyncIoRoot &, const T & node) { return getEdges(node); },
        [&](AsyncIoRoot &, const T & node) { processNode(node); }
    );
}

template<typename T>
kj::Promise<Result<void>> processGraphAsync(
    const std::set<T> & nodes,
    std::function<kj::Promise<Result<std::set<T>>>(const T &)> getEdges,
    std::function<kj::Promise<Result<void>>(const T &)> processNode)
try {
    struct Graph {
        std::set<T> left;
        std::map<T, std::set<T>> refs, rrefs;
    };

    Graph graph{nodes, {}, {}};

    std::function<kj::Promise<Result<void>>(const T &)> worker;

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-capturing-lambda-coroutines)
    worker = [&](const T & node) -> kj::Promise<Result<void>> {
        try {
            auto i = graph.refs.find(node);
            if (i == graph.refs.end())
                goto getRefs;
            goto doWork;

        getRefs:
            {
                auto refs = LIX_TRY_AWAIT(getEdges(node));
                refs.erase(node);

                {
                    for (auto & ref : refs)
                        if (graph.left.count(ref)) {
                            graph.refs[node].insert(ref);
                            graph.rrefs[ref].insert(node);
                        }
                    if (graph.refs[node].empty())
                        goto doWork;
                }
            }

            co_return result::success();

        doWork:
            LIX_TRY_AWAIT(processNode(node));

            /* Enqueue work for all nodes that were waiting on this one
               and have no unprocessed dependencies. */
            std::vector<T> unblocked;
            for (auto & rref : graph.rrefs[node]) {
                auto & refs(graph.refs[rref]);
                auto i = refs.find(node);
                assert(i != refs.end());
                refs.erase(i);
                if (refs.empty())
                    unblocked.push_back(rref);
            }
            graph.left.erase(node);
            graph.refs.erase(node);
            graph.rrefs.erase(node);
            LIX_TRY_AWAIT(asyncSpread(unblocked, worker));
            co_return result::success();
        } catch (...) {
            co_return result::current_exception();
        }
    };

    LIX_TRY_AWAIT(asyncSpread(nodes, worker));

    if (!graph.left.empty())
        throw Error("graph processing incomplete (cyclic reference?)");
    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

}
