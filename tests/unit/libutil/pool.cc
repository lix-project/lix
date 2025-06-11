#include "lix/libutil/pool.hh"
#include "lix/libutil/result.hh"
#include <gtest/gtest.h>
#include <kj/async.h>

namespace nix {

    struct TestResource
    {

        TestResource() {
            static int counter = 0;
            num = counter++;
        }

        int dummyValue = 1;
        bool good = true;
        int num;
    };

    class PoolTest : public testing::Test
    {
    public:
        kj::EventLoop loop;
        kj::WaitScope ws{loop};

        ~PoolTest() noexcept(true) = default;
    };

    /* ----------------------------------------------------------------------------
     * Pool
     * --------------------------------------------------------------------------*/

    TEST_F(PoolTest, freshPoolHasZeroCountAndSpecifiedCapacity) {
        auto isGood = [](const ref<TestResource> & r) { return r->good; };
        auto createResource = []() -> kj::Promise<Result<ref<TestResource>>> {
            return {result::success(make_ref<TestResource>())};
        };

        Pool<TestResource> pool = Pool<TestResource>((size_t)1, createResource, isGood);

        ASSERT_EQ(pool.count(), 0);
        ASSERT_EQ(pool.capacity(), 1);
    }

    TEST_F(PoolTest, freshPoolCanGetAResource) {
        auto isGood = [](const ref<TestResource> & r) { return r->good; };
        auto createResource = []() -> kj::Promise<Result<ref<TestResource>>> {
            return {result::success(make_ref<TestResource>())};
        };

        Pool<TestResource> pool = Pool<TestResource>((size_t)1, createResource, isGood);
        ASSERT_EQ(pool.count(), 0);

        TestResource r = *(pool.get().wait(ws).value());

        ASSERT_EQ(pool.count(), 1);
        ASSERT_EQ(pool.capacity(), 1);
        ASSERT_EQ(r.dummyValue, 1);
        ASSERT_EQ(r.good, true);
    }

    TEST_F(PoolTest, capacityCanBeIncremented) {
        auto isGood = [](const ref<TestResource> & r) { return r->good; };
        auto createResource = []() -> kj::Promise<Result<ref<TestResource>>> {
            return {result::success(make_ref<TestResource>())};
        };

        Pool<TestResource> pool = Pool<TestResource>((size_t)1, createResource, isGood);
        ASSERT_EQ(pool.capacity(), 1);
        pool.incCapacity();
        ASSERT_EQ(pool.capacity(), 2);
    }

    TEST_F(PoolTest, capacityCanBeDecremented) {
        auto isGood = [](const ref<TestResource> & r) { return r->good; };
        auto createResource = []() -> kj::Promise<Result<ref<TestResource>>> {
            return {result::success(make_ref<TestResource>())};
        };

        Pool<TestResource> pool = Pool<TestResource>((size_t)1, createResource, isGood);
        ASSERT_EQ(pool.capacity(), 1);
        pool.decCapacity();
        ASSERT_EQ(pool.capacity(), 0);
    }

    // Test that the resources we allocate are being reused when they are still good.
    TEST_F(PoolTest, reuseResource) {
        auto isGood = [](const ref<TestResource> & r) { return true; };
        auto createResource = []() -> kj::Promise<Result<ref<TestResource>>> {
            return {result::success(make_ref<TestResource>())};
        };

        Pool<TestResource> pool = Pool<TestResource>((size_t)1, createResource, isGood);

        // Compare the instance counter between the two handles. We expect them to be equal
        // as the pool should hand out the same (still) good one again.
        int counter = -1;
        {
            Pool<TestResource>::Handle h = pool.get().wait(ws).value();
            counter = h->num;
        } // the first handle goes out of scope

        { // the second handle should contain the same resource (with the same counter value)
            Pool<TestResource>::Handle h = pool.get().wait(ws).value();
            ASSERT_EQ(h->num, counter);
        }
    }

    // Test that the resources we allocate are being thrown away when they are no longer good.
    TEST_F(PoolTest, badResourceIsNotReused) {
        auto isGood = [](const ref<TestResource> & r) { return false; };
        auto createResource = []() -> kj::Promise<Result<ref<TestResource>>> {
            return {result::success(make_ref<TestResource>())};
        };

        Pool<TestResource> pool = Pool<TestResource>((size_t)1, createResource, isGood);

        // Compare the instance counter between the two handles. We expect them
        // to *not* be equal as the pool should hand out a new instance after
        // the first one was returned.
        int counter = -1;
        {
            Pool<TestResource>::Handle h = pool.get().wait(ws).value();
            counter = h->num;
        } // the first handle goes out of scope

        {
          // the second handle should contain a different resource (with a
          //different counter value)
            Pool<TestResource>::Handle h = pool.get().wait(ws).value();
            ASSERT_NE(h->num, counter);
        }
    }
}
