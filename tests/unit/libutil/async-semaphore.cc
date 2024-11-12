#include "lix/libutil/async-semaphore.hh"

#include <gtest/gtest.h>
#include <kj/async.h>

namespace nix {

TEST(AsyncSemaphore, counting)
{
    kj::EventLoop loop;
    kj::WaitScope waitScope(loop);

    AsyncSemaphore sem(2);

    ASSERT_EQ(sem.available(), 2);
    ASSERT_EQ(sem.used(), 0);

    auto a = kj::evalNow([&] { return sem.acquire(); });
    ASSERT_EQ(sem.available(), 1);
    ASSERT_EQ(sem.used(), 1);
    auto b = kj::evalNow([&] { return sem.acquire(); });
    ASSERT_EQ(sem.available(), 0);
    ASSERT_EQ(sem.used(), 2);

    auto c = kj::evalNow([&] { return sem.acquire(); });
    auto d = kj::evalNow([&] { return sem.acquire(); });

    ASSERT_TRUE(a.poll(waitScope));
    ASSERT_TRUE(b.poll(waitScope));
    ASSERT_FALSE(c.poll(waitScope));
    ASSERT_FALSE(d.poll(waitScope));

    a = nullptr;
    ASSERT_TRUE(c.poll(waitScope));
    ASSERT_FALSE(d.poll(waitScope));

    {
        auto lock = b.wait(waitScope);
        ASSERT_FALSE(d.poll(waitScope));
    }

    ASSERT_TRUE(d.poll(waitScope));

    ASSERT_EQ(sem.available(), 0);
    ASSERT_EQ(sem.used(), 2);
    c = nullptr;
    ASSERT_EQ(sem.available(), 1);
    ASSERT_EQ(sem.used(), 1);
    d = nullptr;
    ASSERT_EQ(sem.available(), 2);
    ASSERT_EQ(sem.used(), 0);
}

TEST(AsyncSemaphore, cancelledWaiter)
{
    kj::EventLoop loop;
    kj::WaitScope waitScope(loop);

    AsyncSemaphore sem(1);

    auto a = kj::evalNow([&] { return sem.acquire(); });
    auto b = kj::evalNow([&] { return sem.acquire(); });
    auto c = kj::evalNow([&] { return sem.acquire(); });

    ASSERT_TRUE(a.poll(waitScope));
    ASSERT_FALSE(b.poll(waitScope));

    b = nullptr;
    a = nullptr;

    ASSERT_TRUE(c.poll(waitScope));
}

}
