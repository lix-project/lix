#include <gtest/gtest.h>
#include <lix/libutil/backoff.hh>

namespace nix {
TEST(Backoff, defaults)
{
    auto initial = 5;
    auto backoff = backoffTimeouts(
        5, std::chrono::seconds(300), std::chrono::seconds(initial), std::chrono::milliseconds(1000)
    );

    BackoffTiming timings = *backoff.next();
    ASSERT_EQ(10000, timings.downloadTimeout.count());
    ASSERT_LE(1500, timings.waitTime.count());
    ASSERT_GE(2500, timings.waitTime.count());

    timings = *backoff.next();
    ASSERT_EQ(20000, timings.downloadTimeout.count());
    ASSERT_LE(3500, timings.waitTime.count());
    ASSERT_GE(4500, timings.waitTime.count());

    ASSERT_TRUE(backoff.next().has_value());

    timings = *backoff.next();
    ASSERT_EQ(80000, timings.downloadTimeout.count());
    ASSERT_LE(15500, timings.waitTime.count());
    ASSERT_GE(16500, timings.waitTime.count());

    ASSERT_FALSE(backoff.next().has_value());
}

TEST(Backoff, capped)
{
    auto initial = 10;
    auto upper = 300;
    auto backoff = backoffTimeouts(
        7,
        std::chrono::seconds(upper),
        std::chrono::seconds(initial),
        std::chrono::milliseconds(1000)
    );

    BackoffTiming timings = *backoff.next();
    *backoff.next();
    *backoff.next();
    *backoff.next();
    *backoff.next();
    timings = *backoff.next();
    ASSERT_EQ(300000, timings.downloadTimeout.count());
    ASSERT_FALSE(backoff.next().has_value());
}
}
