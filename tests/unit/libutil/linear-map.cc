#include "lix/libutil/linear-map.hh"

#include <gtest/gtest.h>

namespace nix {
    TEST(LinearMap, Insert) {
        LinearMap<size_t, int> map;
        ASSERT_EQ(map.insert_or_assign(1, 1).second, false);
        ASSERT_EQ(map.insert_or_assign(3, 5).second, false);
        ASSERT_EQ(map.insert_or_assign(2, 2).second, false);
        ASSERT_EQ(map.insert_or_assign(3, 3).second, true);
        ASSERT_EQ(map.insert_or_assign(4, 4).second, false);

        ASSERT_EQ(map.size(), 4);
        ASSERT_EQ(map.begin()[0], std::pair((size_t) 1, 1));
        ASSERT_EQ(map.begin()[1], std::pair((size_t) 2, 2));
        ASSERT_EQ(map.begin()[2], std::pair((size_t) 3, 3));
        ASSERT_EQ(map.begin()[3], std::pair((size_t) 4, 4));
    }

    TEST(LinearMap, InsertRangeSorted) {
        LinearMap<size_t, int> map;
        std::map<int, int> items = {
            {3, 3},
            {2, 2},
            {1, 1},
            {4, 4},
        };
        map.insert_range_sorted(std::begin(items), std::end(items));

        ASSERT_EQ(map.size(), 4);
        ASSERT_EQ(map.begin()[0], std::pair((size_t) 1, 1));
        ASSERT_EQ(map.begin()[1], std::pair((size_t) 2, 2));
        ASSERT_EQ(map.begin()[2], std::pair((size_t) 3, 3));
        ASSERT_EQ(map.begin()[3], std::pair((size_t) 4, 4));
    }

    TEST(LinearMap, InsertRangeUnsorted) {
        LinearMap<size_t, int> map;
        std::vector<std::pair<int, int>> items = {
            {3, 3},
            {2, 2},
            {1, 1},
            {4, 4},
        };
        map.insert_range(std::begin(items), std::end(items));

        ASSERT_EQ(map.size(), 4);
        ASSERT_EQ(map.begin()[0], std::pair((size_t) 1, 1));
        ASSERT_EQ(map.begin()[1], std::pair((size_t) 2, 2));
        ASSERT_EQ(map.begin()[2], std::pair((size_t) 3, 3));
        ASSERT_EQ(map.begin()[3], std::pair((size_t) 4, 4));
    }

    TEST(LinearMap, InsertRangeDuplicates) {
        LinearMap<size_t, int> map;
        map.insert_or_assign(2, 5);
        std::vector<std::pair<int, int>> items = {
            {3, 3},
            {2, 2},
            {1, 1},
            {4, 4},
        };
        map.insert_range(std::begin(items), std::end(items));

        ASSERT_EQ(map.size(), 4);
        ASSERT_EQ(map.begin()[0], std::pair((size_t) 1, 1));
        ASSERT_EQ(map.begin()[1], std::pair((size_t) 2, 2));
        ASSERT_EQ(map.begin()[2], std::pair((size_t) 3, 3));
        ASSERT_EQ(map.begin()[3], std::pair((size_t) 4, 4));
    }
}
