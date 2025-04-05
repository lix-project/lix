#include "lix/libutil/generator.hh"
#include "lix/libutil/error.hh"

#include <concepts>
#include <cstdint>
#include <gtest/gtest.h>

namespace nix {

namespace {
MakeError(TestError, BaseError);
MakeError(TestError2, BaseError);
}

TEST(Generator, yields)
{
    auto g = []() -> Generator<int> {
        co_yield 1;
        co_yield 2;
    }();

    ASSERT_EQ(g.next(), 1);
    ASSERT_EQ(g.next(), 2);
    ASSERT_FALSE(g.next().has_value());
}

TEST(Generator, returns)
{
    {
        auto g = []() -> Generator<int> { co_return; }();

        ASSERT_FALSE(g.next().has_value());
    }
    {
        auto g = []() -> Generator<int> {
            co_yield 1;
            co_yield []() -> Generator<int> { co_return; }();
            co_yield 2;
            co_yield []() -> Generator<int> { co_yield 10; }();
            co_yield 3;
            (void) "dummy statement to force some more execution";
        }();

        ASSERT_EQ(g.next(), 1);
        ASSERT_EQ(g.next(), 2);
        ASSERT_EQ(g.next(), 10);
        ASSERT_EQ(g.next(), 3);
        ASSERT_FALSE(g.next().has_value());
    }
}

TEST(Generator, nests)
{
    auto g = []() -> Generator<int> {
        co_yield 1;
        co_yield []() -> Generator<int> {
            co_yield 9;
            co_yield []() -> Generator<int> {
                co_yield 99;
                co_yield 100;
            }();
        }();

        auto g2 = []() -> Generator<int> {
            co_yield []() -> Generator<int> {
                co_yield 2000;
                co_yield 2001;
            }();
            co_yield 1001;
        }();

        co_yield g2.next().value();
        co_yield std::move(g2);
        co_yield 2;
    }();

    ASSERT_EQ(g.next(), 1);
    ASSERT_EQ(g.next(), 9);
    ASSERT_EQ(g.next(), 99);
    ASSERT_EQ(g.next(), 100);
    ASSERT_EQ(g.next(), 2000);
    ASSERT_EQ(g.next(), 2001);
    ASSERT_EQ(g.next(), 1001);
    ASSERT_EQ(g.next(), 2);
    ASSERT_FALSE(g.next().has_value());
}

TEST(Generator, nestsExceptions)
{
    auto g = []() -> Generator<int> {
        co_yield 1;
        co_yield []() -> Generator<int> {
            co_yield 9;
            throw TestError("");
            co_yield 10;
        }();
        co_yield 2;
    }();

    ASSERT_EQ(g.next(), 1);
    ASSERT_EQ(g.next(), 9);
    ASSERT_THROW(g.next(), TestError);
}

TEST(Generator, exception)
{
    {
        auto g = []() -> Generator<int> {
            co_yield 1;
            throw TestError("");
        }();

        ASSERT_EQ(g.next(), 1);
        ASSERT_THROW(g.next(), TestError);
        ASSERT_FALSE(g.next().has_value());
    }
    {
        auto g = []() -> Generator<int> {
            throw TestError("");
            co_return;
        }();

        ASSERT_THROW(g.next(), TestError);
        ASSERT_FALSE(g.next().has_value());
    }
}

namespace {
struct Transform
{
    int state = 0;

    std::pair<uint32_t, int> operator()(std::integral auto x)
    {
        return {x, state++};
    }

    Generator<std::pair<uint32_t, int>, Transform> operator()(const char *)
    {
        co_yield 9;
        co_yield 19;
    }

    Generator<std::pair<uint32_t, int>, Transform> operator()(Generator<int> && inner)
    {
        return [](auto g) mutable -> Generator<std::pair<uint32_t, int>, Transform> {
            while (auto i = g.next()) {
                co_yield *i;
            }
        }(std::move(inner));
    }
};
}

TEST(Generator, transform)
{
    auto g = []() -> Generator<std::pair<uint32_t, int>, Transform> {
        co_yield int32_t(-1);
        co_yield "";
        co_yield []() -> Generator<int> { co_yield 7; }();
        co_yield 20;
    }();

    ASSERT_EQ(g.next(), (std::pair<unsigned, int>{4294967295, 0}));
    ASSERT_EQ(g.next(), (std::pair<unsigned, int>{9, 0}));
    ASSERT_EQ(g.next(), (std::pair<unsigned, int>{19, 1}));
    ASSERT_EQ(g.next(), (std::pair<unsigned, int>{7, 0}));
    ASSERT_EQ(g.next(), (std::pair<unsigned, int>{20, 1}));
    ASSERT_FALSE(g.next().has_value());
}

namespace {
struct ThrowTransform
{
    int operator()(int x)
    {
        return x;
    }

    int operator()(bool)
    {
        throw TestError("");
    }

    Generator<int, void> operator()(Generator<int> && inner)
    {
        throw TestError2("");
    }
};
}

TEST(Generator, transformThrows)
{
    {
        auto g = []() -> Generator<int, ThrowTransform> {
            co_yield 1;
            co_yield false;
            co_yield 2;
        }();

        ASSERT_EQ(g.next(), 1);
        ASSERT_THROW(g.next(), TestError);
        ASSERT_FALSE(g.next().has_value());
    }
    {
        auto g = []() -> Generator<int, ThrowTransform> {
            co_yield 1;
            co_yield []() -> Generator<int> {
                co_yield 2;
            }();
            co_yield 3;
        }();

        ASSERT_EQ(g.next(), 1);
        ASSERT_THROW(g.next(), TestError2);
        ASSERT_FALSE(g.next().has_value());
    }
}

TEST(Generator, iterators)
{
    auto g = []() -> Generator<int> {
        for (auto i : {1, 2, 3, 4, 5, 6, 7, 8}) {
            co_yield i;
        }
    }();

    // begin() does not consume an item
    {
        auto it = g.begin();
        ASSERT_EQ(g.next(), 1);
    }

    // operator* consumes only one item per advancement
    {
        auto it = g.begin();
        ASSERT_EQ(*it, 2);
        ASSERT_EQ(*it, 2);
        ++it;
        ASSERT_EQ(*it, 3);
        ASSERT_EQ(*it, 3);
    }

    // not advancing an iterator consumes no items
    ASSERT_EQ(g.next(), 4);

    // operator++ on a fresh iterator consumes *two* items
    {
        auto it = g.begin();
        ++it;
        ASSERT_EQ(g.next(), 7);
    }

    // operator++ on last item reverts to end()
    {
        auto it = g.begin();
        ASSERT_EQ(*it, 8);
        ASSERT_NE(it, g.end());
        ++it;
        ASSERT_EQ(it, g.end());
    }
}

TEST(Generator, nonDefaultCtor)
{
    auto g = []() -> Generator<std::reference_wrapper<int>> {
        int i = 0;
        co_yield i;
        i += 1;
        co_yield i;
    }();

    auto i = g.next();
    ASSERT_EQ(*i, 0);
    i->get() = 10;
    i = g.next();
    ASSERT_EQ(*i, 11);
}

}
