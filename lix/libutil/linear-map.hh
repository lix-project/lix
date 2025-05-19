#pragma once
///@file

#include <functional>
#include <vector>
#include <algorithm>

namespace nix {
/**
 * Provides a map-like data structure but backed by a vector.
 * The comparison `Comp` must be a linear order on `K`.
 * This is mostly used for mapping symbols to values in the expression tree,
 * where the data structure is immutable after having been built and fast linear
 * access is important.
 */
template<typename K, typename V, typename Comp = std::less<>>
class LinearMap : private std::vector<std::pair<K, V>>
{
private:
    using base = std::vector<std::pair<K, V>>;

    auto lower_bound_of(const K & key)
    {
        return std::lower_bound(
            base::begin(),
            base::end(),
            key,
            [](const std::pair<K, V> & pair, const K & k) { return Comp{}(pair.first, k); }
        );
    }
    auto lower_bound_of(const K & key) const
    {
        return std::lower_bound(
            base::begin(),
            base::end(),
            key,
            [](const std::pair<K, V> & pair, const K & k) { return Comp{}(pair.first, k); }
        );
    }

public:
    using typename base::const_iterator, typename base::value_type;
    LinearMap() = default;
    LinearMap(size_t expectedSize)
    {
        reserve(expectedSize);
    }
    using base::size, base::reserve, base::clear, base::cbegin, base::cend;

    /* Insert an element at the correct position, shifting later elements back by
     * one place. Returns `true` if a previous element with that key was
     * overwritten
     */
    std::pair<const_iterator, bool> insert_or_assign(const K key, V value)
    {
        /* Fast path: We are inserting elements in order and thus the insertion is bigger than the
         * current last element */
        if (base::empty() || Comp{}(base::back().first, key)) {
            base::emplace_back(std::move(key), std::move(value));
            return std::pair(base::end() - 1, false);
        }

        auto i = lower_bound_of(key);
        if (i != end() && i->first == key) {
            i->second = std::move(value);
            return std::pair(i, true);
        } else {
            auto i2 = base::insert(i, std::pair(key, std::move(value)));
            return std::pair(i2, false);
        }
    }

    /* Insert an arbitrary amount of values into the map with a callable function.
     * This is a workaround to C++'s sad state of iterators and generators.
     * The passed function gets access to the internal backing vector, and may append any number of
     * elements to it. It is up to the passed function to ensure only elements are appended, and
     * that the set of added elements is ordered and free of duplicates. After the insertion
     * function terminates, the added items are merged into the map in O(n). Newly inserted elements
     * override existing elements in the map.
     */
    template<typename Fn>
    void unsafe_insert_bulk(Fn func)
    {
        auto oldSize = this->size();
        func(static_cast<base &>(*this));

        std::inplace_merge(
            base::begin(),
            base::begin() + oldSize,
            base::end(),
            [](const auto & a, const auto & b) { return Comp{}(a.first, b.first); }
        );

        /* Deduplicate, prefer newly inserted */
        auto it = base::begin(), jt = it;
        while (jt != base::end()) {
            *it = *jt++;
            while (jt != base::end() && it->first == jt->first) {
                *it = *jt++;
            }
            it++;
        }
        base::erase(it, base::end());
    }

    /* The inserted range must be sorted and free of duplicates */
    template<typename It>
    void insert_range_sorted(It it, It end)
    {
        unsafe_insert_bulk([&](auto & map) {
            if constexpr (requires { end - it; }) {
                map.reserve(map.size() + (end - it));
            }
            for (; it != end; ++it) {
                map.emplace_back(it->first, it->second);
            }
        });
    };

    /* The inserted range must be free of duplicates */
    template<typename It>
    void insert_range(It it, It end)
    {
        unsafe_insert_bulk([&](auto & map) {
            auto oldSize = map.size();
            if constexpr (requires { end - it; }) {
                map.reserve(map.size() + (end - it));
            }
            for (; it != end; ++it) {
                map.emplace_back(it->first, it->second);
            }
            std::sort(map.begin() + oldSize, map.end(), Comp{});
        });
    };

    const_iterator find(const K & key) const
    {
        if (auto i = lower_bound_of(key); i != base::end() && i->first == key) {
            return i;
        }
        return base::end();
    }
    const_iterator begin() const
    {
        return cbegin();
    }
    const_iterator end() const
    {
        return cend();
    }
};
} // namespace nix
