#pragma once
///@file

#include "types.hh"
#include "error.hh"
#include "logging.hh"
#include "ansicolor.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>

#include <boost/lexical_cast.hpp>

#include <atomic>
#include <functional>
#include <map>
#include <sstream>
#include <optional>

#ifndef HAVE_STRUCT_DIRENT_D_TYPE
#define DT_UNKNOWN 0
#define DT_REG 1
#define DT_LNK 2
#define DT_DIR 3
#endif

namespace nix {

struct Sink;
struct Source;

/**
 * The system for which Nix is compiled.
 */
extern const std::string nativeSystem;


std::string getUserName();

/**
 * @return the given user's home directory from /etc/passwd.
 */
Path getHomeOf(uid_t userId);

/**
 * @return $HOME or the user's home directory from /etc/passwd.
 */
Path getHome();

/**
 * @return $XDG_CACHE_HOME or $HOME/.cache.
 */
Path getCacheDir();

/**
 * @return $XDG_CONFIG_HOME or $HOME/.config.
 */
Path getConfigDir();

/**
 * @return the directories to search for user configuration files
 */
std::vector<Path> getConfigDirs();

/**
 * @return $XDG_DATA_HOME or $HOME/.local/share.
 */
Path getDataDir();

/**
 * @return $XDG_STATE_HOME or $HOME/.local/state.
 *
 * @note Not to be confused with settings.nixStateDir.
 */
Path getStateDir();

/**
 * Create $XDG_STATE_HOME/nix or $HOME/.local/state/nix, and return
 * the path to it.
 * @note Not to be confused with settings.nixStateDir.
 */
Path createNixStateDir();


/**
 * Save the current mount namespace. Ignored if called more than
 * once.
 */
void saveMountNamespace();

/**
 * Restore the mount namespace saved by saveMountNamespace(). Ignored
 * if saveMountNamespace() was never called.
 */
void restoreMountNamespace();

/**
 * Cause this thread to not share any FS attributes with the main
 * thread, because this causes setns() in restoreMountNamespace() to
 * fail.
 */
void unshareFilesystem();


/**
 * Exception handling in destructors: print an error message, then
 * ignore the exception.
 */
void ignoreException(Verbosity lvl = lvlError);



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


}
