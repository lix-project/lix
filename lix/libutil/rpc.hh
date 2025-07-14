#pragma once
///@file RPC helper functions

#include "lix/libutil/charptr-cast.hh"
#include "lix/libutil/rpc-fwd.hh"
#include <capnp/blob.h>
#include <capnp/common.h>
#include <capnp/list.h>
#include <concepts>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>

namespace nix::rpc {

template<typename To, typename From>
    requires requires(From f) { Convert<typename From::Reads, To>{}; }
To to(const From & from, auto &&... args)
{
    return Convert<typename From::Reads, To>::convert(from, args...);
}

// conversions for capnp Text
template<>
struct Convert<capnp::Text, std::string_view>
{
    static std::string_view convert(const capnp::Text::Reader & t, auto &&...)
    {
        return std::string_view(t.begin(), t.size());
    }
};

template<>
struct Convert<capnp::Text, std::string>
{
    static std::string convert(const capnp::Text::Reader & t, auto &&...)
    {
        return std::string(to<std::string_view>(t));
    }
};

// conversions for capnp Data
template<>
struct Convert<capnp::Data, std::string_view>
{
    static std::string_view convert(const capnp::Data::Reader & t, auto &&...)
    {
        return std::string_view(t.asChars().begin(), t.size());
    }
};

template<>
struct Convert<capnp::Data, std::string>
{
    static std::string convert(const capnp::Data::Reader & t, auto &&...)
    {
        return std::string(to<std::string_view>(t));
    }
};

// conversions for collections. order is maintained during conversion and turns
// into insertion order for the result, i.e. sets and maps will only retain the
// first element in the list that compares equivalent to some other list entry.
template<typename Elem, typename To>
    requires requires(To t, capnp::List<Elem>::Reader list) {
        typename To::value_type;
        t.insert(t.end(), to<typename To::value_type>(list[0]));
    }
struct Convert<capnp::List<Elem>, To>
{
    static To convert(const capnp::List<Elem>::Reader & list, auto &&... args)
    {
        To result;
        for (auto && t : list) {
            result.insert(result.end(), to<typename To::value_type>(t, args...));
        }
        return result;
    }
};

template<typename From>
    requires std::convertible_to<std::ranges::range_value_t<From>, std::string>
struct Fill<capnp::List<capnp::Text>, From>
{
    static void fill(capnp::List<capnp::Text>::Builder builder, const From & from, auto &&... args)
    {
        size_t i = 0;
        for (const std::string & e : from) {
            builder.set(i++, e);
        }
    }
};

template<typename From>
    requires std::convertible_to<std::ranges::range_value_t<From>, std::string_view>
struct Fill<capnp::List<capnp::Data>, From>
{
    static void fill(capnp::List<capnp::Data>::Builder builder, const From & from, auto &&... args)
    {
        size_t i = 0;
        for (auto && e : from) {
            std::string_view sv = e;
            builder.set(i++, {charptr_cast<const unsigned char *>(sv.begin()), sv.size()});
        }
    }
};

template<typename Elem, typename From>
    requires requires { Fill<Elem, std::ranges::range_value_t<From>>{}; }
struct Fill<capnp::List<Elem>, From>
{
    static void fill(capnp::List<Elem>::Builder builder, const From & from, auto &&... args)
    {
        size_t i = 0;
        for (auto && e : from) {
            Fill<Elem, std::ranges::range_value_t<From>>::fill(builder[i++], e, args...);
        }
    }
};

// blanket converter that turns a `from` into a `to`
template<typename Rpc, typename To>
    requires requires(typename Rpc::Reader r) {
        {
            from(r)
        } -> std::same_as<To>;
    }
struct Convert<Rpc, To>
{
    static To convert(const typename Rpc::Reader & r, auto &&... args)
    {
        return from(r, args...);
    }
};

namespace detail {
// strings to Data and Text
template<typename Builder, std::convertible_to<std::string_view> From>
inline void
doFill(auto && builder, void (Builder::*Field)(::capnp::Data::Reader), From && f, auto &&...)
{
    std::string_view sv = f;
    (builder.*Field)({charptr_cast<const unsigned char *>(sv.begin()), sv.size()});
}

template<typename Builder, std::convertible_to<std::string> From>
inline void
doFill(auto && builder, void (Builder::*field)(::capnp::Text::Reader), From && f, auto &&...)
{
    (builder.*field)(f);
}

// structs
template<typename Builder, typename From, typename Inner>
inline void doFill(auto && builder, Inner (Builder::*field)(), From && f, auto &&... args)
{
    Fill<typename Inner::Builds, std::remove_cvref_t<From>>::fill(
        (builder.*field)(), std::forward<From>(f), args...
    );
}

// lists and other primitives. lists must be distinguished by having builders.
template<typename Builder, typename From, typename Inner, typename Init>
inline void doFill(auto && builder, Inner (Builder::*field)(Init), From && f, auto &&... args)
{
    if constexpr (requires {
                      std::same_as<Init, capnp::uint>;
                      typename Inner::Builds;
                  })
    {
        Fill<typename Inner::Builds, std::remove_cvref_t<From>>::fill(
            (builder.*field)(f.size()), std::forward<From>(f), args...
        );
    } else {
        (builder.*field)(std::forward<From>(f));
    }
}
}

#define LIX_RPC_FILL(fobj, ffield, fsource, ...) \
    [&] { ::nix::rpc::detail::doFill(fobj, &decltype(fobj)::ffield, (fsource), ##__VA_ARGS__); }()

#define LIX_TRY_AWAIT_RPC(...)                                    \
    LIX_TRY_AWAIT_CONTEXT_MAP(                                    \
        [] { return "RPC call"; },                                \
        ([](auto r) { return ::nix::rpc::from(r.getResult()); }), \
        __VA_ARGS__                                               \
    )

#ifdef LIX_UR_COMPILER_UWU
#define RPC_FILL LIX_RPC_FILL
#define TRY_AWAIT_RPC LIX_TRY_AWAIT_RPC
#endif
}
