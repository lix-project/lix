#pragma once
///@file

#include "ref.hh"

#include <list>
#include <optional>
#include <set>
#include <string>
#include <limits>
#include <map>
#include <variant>
#include <vector>

namespace nix {

typedef std::list<std::string> Strings;
typedef std::set<std::string> StringSet;
typedef std::map<std::string, std::string> StringMap;
typedef std::map<std::string, std::string> StringPairs;

/**
 * Paths are just strings.
 */
typedef std::string Path;
typedef std::string_view PathView;
typedef std::list<Path> Paths;
typedef std::set<Path> PathSet;

typedef std::vector<std::pair<std::string, std::string>> Headers;

/**
 * Helper class to run code at startup.
 */
template<typename T>
struct OnStartup
{
    OnStartup(T && t) { t(); }
};

/**
 * Wrap bools to prevent string literals (i.e. 'char *') from being
 * cast to a bool in Attr.
 */
template<typename T>
struct Explicit {
    T t;

    bool operator ==(const Explicit<T> & other) const
    {
        return t == other.t;
    }
};

/**
 * Get a value for the specified key from an associate container.
 */
template <class T>
const typename T::mapped_type * get(const T & map, const typename T::key_type & key)
{
    auto i = map.find(key);
    if (i == map.end()) return nullptr;
    return &i->second;
}

template <class T>
typename T::mapped_type * get(T & map, const typename T::key_type & key)
{
    auto i = map.find(key);
    if (i == map.end()) return nullptr;
    return &i->second;
}

/**
 * Get a value for the specified key from an associate container, or a default value if the key isn't present.
 */
template <class T>
const typename T::mapped_type & getOr(T & map,
    const typename T::key_type & key,
    const typename T::mapped_type & defaultValue)
{
    auto i = map.find(key);
    if (i == map.end()) return defaultValue;
    return i->second;
}

/**
 * Remove and return the first item from a container.
 */
template <class T>
std::optional<typename T::value_type> remove_begin(T & c)
{
    auto i = c.begin();
    if (i == c.end()) return {};
    auto v = std::move(*i);
    c.erase(i);
    return v;
}


/**
 * Remove and return the first item from a container.
 */
template <class T>
std::optional<typename T::value_type> pop(T & c)
{
    if (c.empty()) return {};
    auto v = std::move(c.front());
    c.pop();
    return v;
}


/**
 * A RAII helper that increments a counter on construction and
 * decrements it on destruction.
 */
template<typename T>
struct MaintainCount
{
    T & counter;
    long delta;
    MaintainCount(T & counter, long delta = 1) : counter(counter), delta(delta) { counter += delta; }
    ~MaintainCount() { counter -= delta; }
};


/**
 * A Rust/Python-like enumerate() iterator adapter.
 *
 * Borrowed from http://reedbeta.com/blog/python-like-enumerate-in-cpp17.
 */
template <typename T,
          typename TIter = decltype(std::begin(std::declval<T>())),
          typename = decltype(std::end(std::declval<T>()))>
constexpr auto enumerate(T && iterable)
{
    struct iterator
    {
        size_t i;
        TIter iter;
        constexpr bool operator != (const iterator & other) const { return iter != other.iter; }
        constexpr void operator ++ () { ++i; ++iter; }
        constexpr auto operator * () const { return std::tie(i, *iter); }
    };

    struct iterable_wrapper
    {
        T iterable;
        constexpr auto begin() { return iterator{ 0, std::begin(iterable) }; }
        constexpr auto end() { return iterator{ 0, std::end(iterable) }; }
    };

    return iterable_wrapper{ std::forward<T>(iterable) };
}


/**
 * C++17 std::visit boilerplate
 */
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...) -> overloaded<Ts...>;



/**
 * This wants to be a little bit like rust's Cow type.
 * Some parts of the evaluator benefit greatly from being able to reuse
 * existing allocations for strings, but have to be able to also use
 * newly allocated storage for values.
 *
 * We do not define implicit conversions, even with ref qualifiers,
 * since those can easily become ambiguous to the reader and can degrade
 * into copying behaviour we want to avoid.
 */
class BackedStringView {
private:
    std::variant<std::string, std::string_view> data;

    /**
     * Needed to introduce a temporary since operator-> must return
     * a pointer. Without this we'd need to store the view object
     * even when we already own a string.
     */
    class Ptr {
    private:
        std::string_view view;
    public:
        Ptr(std::string_view view): view(view) {}
        const std::string_view * operator->() const { return &view; }
    };

public:
    BackedStringView(std::string && s): data(std::move(s)) {}
    BackedStringView(std::string_view sv): data(sv) {}
    template<size_t N>
    BackedStringView(const char (& lit)[N]): data(std::string_view(lit)) {}

    BackedStringView(const BackedStringView &) = delete;
    BackedStringView & operator=(const BackedStringView &) = delete;

    /**
     * We only want move operations defined since the sole purpose of
     * this type is to avoid copies.
     */
    BackedStringView(BackedStringView && other) = default;
    BackedStringView & operator=(BackedStringView && other) = default;

    bool isOwned() const
    {
        return std::holds_alternative<std::string>(data);
    }

    std::string toOwned() &&
    {
        return isOwned()
            ? std::move(std::get<std::string>(data))
            : std::string(std::get<std::string_view>(data));
    }

    std::string_view operator*() const
    {
        return isOwned()
            ? std::get<std::string>(data)
            : std::get<std::string_view>(data);
    }
    Ptr operator->() const { return Ptr(**this); }
};

}
