#include "lix/libutil/thread-pool.hh"
#include "lix/libutil/logging.hh"
#include "lix/libutil/signals.hh"
#include "lix/libutil/thread-name.hh"
#include <kj/common.h>

namespace nix {

ThreadPool::ThreadPool(const char * name, size_t _maxThreads)
    : maxThreads(_maxThreads), name(name)
{
    if (!maxThreads) {
        maxThreads = std::thread::hardware_concurrency();
        if (!maxThreads) maxThreads = 1;
    }

    debug("starting pool of %d threads", maxThreads);
}

ThreadPool::~ThreadPool()
{
    shutdown();
}

void ThreadPool::shutdown()
{
    std::vector<std::thread> workers;
    {
        auto state(state_.lock());
        quit = true;
        std::swap(workers, state->workers);
    }

    if (workers.empty()) return;

    debug("reaping %d worker threads", workers.size());

    work.notify_all();

    for (auto & thr : workers)
        thr.join();
}

void ThreadPool::enqueueWithAio(const work_t & t)
{
    auto state(state_.lock());
    if (quit)
        throw ThreadPoolShutDown("cannot enqueue a work item while the thread pool is shutting down");
    state->pending.push(t);
    if (state->active == state->workers.size() && state->workers.size() < maxThreads)
        state->workers.emplace_back(&ThreadPool::doWork, this);
    work.notify_one();
}

void ThreadPool::process()
{
    const auto shouldWait = [&] {
        auto state(state_.lock());
        state->draining = true;
        return state->active > 0 || !state->pending.empty();
    }();

    /* Wait until no more work is pending or active. */
    try {
        if (shouldWait) {
            quit.wait(false);
        }

        auto state(state_.lock());
        if (state->exception)
            std::rethrow_exception(state->exception);

    } catch (...) {
        /* In the exceptional case, some workers may still be
           active. They may be referencing the stack frame of the
           caller. So wait for them to finish. (~ThreadPool also does
           this, but it might be destroyed after objects referenced by
           the work item lambdas.) */
        shutdown();
        throw;
    }
}

kj::Promise<Result<void>> ThreadPool::processAsync()
try {
    auto [shouldWait, signal] = [&] {
        auto state = state_.lock();
        state->draining = true;
        auto pfp = kj::newPromiseAndCrossThreadFulfiller<void>();
        state->anyWorkerExited = std::move(pfp.fulfiller);
        return std::pair(state->active > 0 || !state->pending.empty(), std::move(pfp.promise));
    }();

    KJ_DEFER({
        if (std::uncaught_exceptions()) {
            /* In the exceptional case, some workers may still be
               active. They may be referencing the stack frame of the
               caller. So wait for them to finish. (~ThreadPool also does
               this, but it might be destroyed after objects referenced by
               the work item lambdas.) */
            shutdown();
        }
    });

    /* Wait until no more work is pending or active. */
    if (shouldWait && !quit) {
        co_await signal;
    }

    auto state(state_.lock());
    if (state->exception) {
        std::rethrow_exception(state->exception);
    }

    co_return result::success();
} catch (...) {
    co_return result::current_exception();
}

void ThreadPool::doWork()
{
    // Tell the other workers to quit; we only return on errors or completion
    KJ_DEFER({
        quit = true;
        quit.notify_all();
        if (auto state = state_.lock(); state->anyWorkerExited) {
            (*state->anyWorkerExited)->fulfill();
            state->anyWorkerExited.reset();
        }
        work.notify_all();
    });
    ReceiveInterrupts receiveInterrupts;

    setCurrentThreadName(this->name);
    interruptCheck = [&]() { return (bool) quit; };

    bool didWork = false;
    std::exception_ptr exc;

    AsyncIoRoot aio;

    while (true) {
        work_t w;
        {
            auto state(state_.lock());

            if (didWork) {
                assert(state->active);
                state->active--;

                if (exc) {

                    if (!state->exception) {
                        state->exception = exc;
                        return;
                    } else {
                        /* Print the exception, since we can't
                           propagate it. */
                        try {
                            std::rethrow_exception(exc);
                        } catch (ThreadPoolShutDown &) {
                        } catch (...) {
                            // Yes, this is not a destructor, but we cannot
                            // safely propagate an exception out of here.
                            //
                            // What happens is that if we do, shutdown()
                            // will have join() throw an exception if we
                            // are on a worker thread, preventing us from
                            // joining the rest of the threads. Although we
                            // could make the joining eat exceptions too,
                            // we could just as well not let Interrupted
                            // fall out to begin with, since the thread
                            // will immediately cleanly quit because of
                            // quit == true anyway.
                            ignoreExceptionInDestructor();
                        }
                    }
                }
            }

            /* Wait until a work item is available or we're asked to
               quit. */
            while (true) {
                if (quit) return;

                if (!state->pending.empty()) break;

                /* If there are no active or pending items, and the
                   main thread is running process(), then no new items
                   can be added. So exit. */
                if (!state->active && state->draining) {
                    return;
                }

                state.wait(work);
            }

            w = std::move(state->pending.front());
            state->pending.pop();
            state->active++;
        }

        try {
            w(aio);
        } catch (...) {
            exc = std::current_exception();
        }

        didWork = true;
    }
}

}
