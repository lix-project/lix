#include "lix/libutil/thread-pool.hh"
#include "lix/libutil/async.hh"
#include "lix/libutil/error.hh"
#include <atomic>
#include <exception>
#include <gtest/gtest.h>
#include <memory>

static auto onThreadExit(auto fn)
{
    auto deferred = kj::defer(std::move(fn));
    return std::make_shared<decltype(deferred)>(std::move(deferred));
}

namespace nix {

TEST(ThreadPool, creates_threads)
{
    ThreadPool t{"test", 2};

    std::atomic_bool unblockA{false}, unblockB{false};
    std::atomic_bool started{false};

    t.enqueue([&] {
        started = true;
        started.notify_all();
        unblockA.wait(false);
        unblockB = true;
        unblockB.notify_all();
    });
    started.wait(false);

    // now no work is pending. the next enqueue should start a
    // new thread; if it does not we'll deadlock and time out.

    started = false;
    t.enqueue([&] {
        started = true;
        started.notify_all();
        unblockB.wait(false);
    });
    started.wait(false);

    unblockA = true;
    unblockA.notify_all();

    t.process();
}

TEST(ThreadPool, early_quit)
{
    ThreadPool t{"test", 2};
    bool ran_anyway = false;

    struct Dead : BaseException {};

    std::atomic_bool unblockA{false}, unblockB{false};
    std::atomic_bool started{false};

    t.enqueue([&] {
        started = true;
        started.notify_all();
        unblockA.wait(false);
        thread_local auto _ = onThreadExit([&] {
            unblockB = true;
            unblockB.notify_all();
        });
        throw Dead{};
    });
    started.wait(false);

    started = false;
    t.enqueue([&] {
        started = true;
        started.notify_all();
        unblockB.wait(false);
    });
    started.wait(false);

    // this one should never run. the first thread saw an exception,
    // and the second thread should have exited early because of it.
    t.enqueue([&] { ran_anyway = true; });

    unblockA = true;
    unblockA.notify_all();

    ASSERT_THROW(t.process(), Dead);
    ASSERT_FALSE(ran_anyway);
}

TEST(ThreadPool, early_quit_async)
{
    ThreadPool t{"test", 2};
    bool ran_anyway = false;

    struct Dead : BaseException {};

    std::atomic_bool unblockA{false}, unblockB{false};
    std::atomic_bool started{false};

    t.enqueue([&] {
        started = true;
        started.notify_all();
        unblockA.wait(false);
        thread_local auto _ = onThreadExit([&] {
            unblockB = true;
            unblockB.notify_all();
        });
        throw Dead{};
    });
    started.wait(false);

    started = false;
    t.enqueue([&] {
        started = true;
        started.notify_all();
        unblockB.wait(false);
    });
    started.wait(false);

    // this one should never run. the first thread saw an exception,
    // and the second thread should have exited early because of it.
    t.enqueue([&] { ran_anyway = true; });

    unblockA = true;
    unblockA.notify_all();

    AsyncIoRoot aio;
    ASSERT_THROW(aio.blockOn(t.processAsync()), Dead);
    ASSERT_FALSE(ran_anyway);
}

TEST(ThreadPool, always_rethrows)
{
    ThreadPool t{"test"};

    struct Dead : BaseException {};

    std::atomic_bool flag{false};

    t.enqueue([&] {
        thread_local auto _ = onThreadExit([&] {
            flag = true;
            flag.notify_all();
        });

        throw Dead{};
    });

    flag.wait(false);

    ASSERT_THROW(t.process(), Dead);
}

TEST(ThreadPool, always_rethrows_async)
{
    ThreadPool t{"test"};

    struct Dead : BaseException {};

    std::atomic_bool flag{false};

    t.enqueue([&] {
        thread_local auto _ = onThreadExit([&] {
            flag = true;
            flag.notify_all();
        });

        throw Dead{};
    });

    flag.wait(false);

    AsyncIoRoot aio;
    ASSERT_THROW(aio.blockOn(t.processAsync()), Dead);
}

}
