#include "lix/libutil/async-collect.hh"

#include <gtest/gtest.h>
#include <kj/array.h>
#include <kj/async.h>
#include <kj/exception.h>
#include <stdexcept>

namespace nix {

TEST(AsyncCollect, void)
{
    kj::EventLoop loop;
    kj::WaitScope waitScope(loop);

    auto a = kj::newPromiseAndFulfiller<void>();
    auto b = kj::newPromiseAndFulfiller<void>();
    auto c = kj::newPromiseAndFulfiller<void>();
    auto d = kj::newPromiseAndFulfiller<void>();

    auto collect = asyncCollect(kj::arr(
        std::pair(1, std::move(a.promise)),
        std::pair(2, std::move(b.promise)),
        std::pair(3, std::move(c.promise)),
        std::pair(4, std::move(d.promise))
    ));

    auto p = collect.next();
    ASSERT_FALSE(p.poll(waitScope));

    // collection is ordered
    c.fulfiller->fulfill();
    b.fulfiller->fulfill();

    ASSERT_TRUE(p.poll(waitScope));
    ASSERT_EQ(p.wait(waitScope), 3);

    p = collect.next();
    ASSERT_TRUE(p.poll(waitScope));
    ASSERT_EQ(p.wait(waitScope), 2);

    p = collect.next();
    ASSERT_FALSE(p.poll(waitScope));

    // exceptions propagate
    a.fulfiller->rejectIfThrows([] {
        throw std::runtime_error("test"); // NOLINT(lix-foreign-exceptions)
    });

    p = collect.next();
    ASSERT_TRUE(p.poll(waitScope));
    ASSERT_THROW(p.wait(waitScope), kj::Exception);

    // first exception aborts collection
    p = collect.next();
    ASSERT_TRUE(p.poll(waitScope));
    ASSERT_THROW(p.wait(waitScope), kj::Exception);
}

TEST(AsyncCollect, nonVoid)
{
    kj::EventLoop loop;
    kj::WaitScope waitScope(loop);

    auto a = kj::newPromiseAndFulfiller<int>();
    auto b = kj::newPromiseAndFulfiller<int>();
    auto c = kj::newPromiseAndFulfiller<int>();
    auto d = kj::newPromiseAndFulfiller<int>();

    auto collect = asyncCollect(kj::arr(
        std::pair(1, std::move(a.promise)),
        std::pair(2, std::move(b.promise)),
        std::pair(3, std::move(c.promise)),
        std::pair(4, std::move(d.promise))
    ));

    auto p = collect.next();
    ASSERT_FALSE(p.poll(waitScope));

    // collection is ordered
    c.fulfiller->fulfill(1);
    b.fulfiller->fulfill(2);

    ASSERT_TRUE(p.poll(waitScope));
    ASSERT_EQ(p.wait(waitScope), std::pair(3, 1));

    p = collect.next();
    ASSERT_TRUE(p.poll(waitScope));
    ASSERT_EQ(p.wait(waitScope), std::pair(2, 2));

    p = collect.next();
    ASSERT_FALSE(p.poll(waitScope));

    // exceptions propagate
    a.fulfiller->rejectIfThrows([] {
        throw std::runtime_error("test"); // NOLINT(lix-foreign-exceptions)
    });

    p = collect.next();
    ASSERT_TRUE(p.poll(waitScope));
    ASSERT_THROW(p.wait(waitScope), kj::Exception);

    // first exception aborts collection
    p = collect.next();
    ASSERT_TRUE(p.poll(waitScope));
    ASSERT_THROW(p.wait(waitScope), kj::Exception);
}
}
