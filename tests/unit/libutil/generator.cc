#include "lix/libutil/generator.hh"

#include <concepts>
#include <cstdint>
#include <gtest/gtest.h>

namespace nix {

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
            // NOLINTNEXTLINE(hicpp-exception-baseclass)
            throw 1;
            co_yield 10;
        }();
        co_yield 2;
    }();

    ASSERT_EQ(g.next(), 1);
    ASSERT_EQ(g.next(), 9);
    ASSERT_THROW(g.next(), int);
}

TEST(Generator, exception)
{
    {
        auto g = []() -> Generator<int> {
            co_yield 1;
            // NOLINTNEXTLINE(hicpp-exception-baseclass)
            throw 1;
        }();

        ASSERT_EQ(g.next(), 1);
        ASSERT_THROW(g.next(), int);
        ASSERT_FALSE(g.next().has_value());
    }
    {
        auto g = []() -> Generator<int> {
            // NOLINTNEXTLINE(hicpp-exception-baseclass)
            throw 1;
            co_return;
        }();

        ASSERT_THROW(g.next(), int);
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
        // NOLINTNEXTLINE(hicpp-exception-baseclass)
        throw 2;
    }

    Generator<int, void> operator()(Generator<int> && inner)
    {
        // NOLINTNEXTLINE(hicpp-exception-baseclass)
        throw false;
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
        ASSERT_THROW(g.next(), int);
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
        ASSERT_THROW(g.next(), bool);
        ASSERT_FALSE(g.next().has_value());
    }
}

}
