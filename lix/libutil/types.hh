#pragma once
///@file

#include <list>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <map>
#include <vector>
#include <span>
#include <stdint.h> // IWYU pragma: keep (this is used literally everywhere)

namespace nix {

typedef std::list<std::string> Strings;
typedef std::set<std::string> StringSet;
typedef std::map<std::string, std::string> StringMap;
typedef std::map<std::string, std::string> StringPairs;

// TODO this should be a std::byte span, but too much of the
// current codebase predates std::byte and uses char instead
using Bytes = std::span<const char>;

/**
 * Paths are just strings.
 */
typedef std::string Path;
typedef std::string_view PathView;
typedef std::list<Path> Paths;
typedef std::set<Path> PathSet;

typedef std::vector<std::pair<std::string, std::string>> Headers;

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
template<class T>
const typename T::mapped_type & getOr(
    T & map [[clang::lifetimebound]],
    const typename T::key_type & key,
    const typename T::mapped_type & defaultValue [[clang::lifetimebound]]
)
{
    auto i = map.find(key);
    if (i == map.end()) {
        /* FIXME(Raito): `[[clang::lifetimebound]]` has no effect on `defaultValue` warning. */
        // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter)
        return defaultValue;
    }
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
 *
 * FIXME(jade): remove once P2164R9 is implemented in libc++ and replace with
 * std::views::enumerate: https://libcxx.llvm.org/Status/Cxx23.html
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
        constexpr auto operator * () { return std::tie(i, *iter); }
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
 * marker type for things that should never be called from async code.
 * add a defaulted argument of this type to a method or constructor to
 * have our linter check that marked code is never called by accident.
 */
struct NeverAsync {};

/**
 * Escape hatch to allow calling NeverAsync-marked code from functions
 * that aren't themselves NeverAsync. this should only be used when no
 * typelevel proof can be given for a call that's dynamically known to
 * not block. using this is still forbidden in promises since blocking
 * an executor, even on something known to complete, impedes progress.
 */
constexpr inline NeverAsync always_progresses;

}
