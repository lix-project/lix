#pragma once
///@file RPC helper functions

#include "lix/libutil/result.hh"
#include "lix/libutil/charptr-cast.hh"
#include "lix/libutil/rpc-fwd.hh"
#include <capnp/blob.h>
#include <capnp/common.h>
#include <capnp/list.h>
#include <concepts>
#include <kj/async.h>
#include <ranges>
#include <source_location>
#include <string>
#include <string_view>
#include <type_traits>

namespace kj {
class Exception;
}

namespace nix::rpc {

template<typename To, typename From>
    requires requires(From f) { Convert<typename From::Reads, To>{}; }
To to(const From & from, auto &&... args)
{
    return Convert<typename From::Reads, To>::convert(from, args...);
}

template<typename T>
    requires std::integral<T> || std::floating_point<T> || std::same_as<T, bool>
T from(T t, auto &&...)
{
    return t;
}

// blanket converter that turns a `from` into a `to`
template<typename Rpc, typename To>
struct Convert
{
    template<typename... Args>
        requires requires(typename Rpc::Reader r, Args... args) {
            { from(r, args...) } -> std::same_as<To>;
        }
    static To convert(const typename Rpc::Reader & r, Args &&... args)
    {
        return from(r, args...);
    }
};

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
    requires requires {
        requires std::same_as<Init, capnp::uint>;
        typename Inner::Builds;
    } || std::convertible_to<From, Init>
inline void doFill(auto && builder, Inner (Builder::*field)(Init), From && f, auto &&... args)
{
    if constexpr (requires {
                      requires std::same_as<Init, capnp::uint>;
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

template<typename T>
inline constexpr bool IsRpcListV = false;
template<typename T, capnp::Kind K>
inline constexpr bool IsRpcListV<capnp::List<T, K>> = true;

template<typename T>
concept RpcList = IsRpcListV<T>;
}

#define LIX_RPC_FILL(fobj, ffield, fsource, ...) \
    (::nix::rpc::detail::doFill(fobj, &decltype(fobj)::ffield, (fsource), ##__VA_ARGS__))

#define LIX_RPC_FILL_LIST(fobj, ffield, fsource, ...)                                         \
    (::nix::rpc::detail::doFill(                                                              \
        fobj,                                                                                 \
        ([]<typename Builder, typename Inner, typename Init>(Inner (Builder::*field)(Init)) { \
            return field;                                                                     \
        })(&decltype(fobj)::ffield),                                                          \
        (fsource),                                                                            \
        ##__VA_ARGS__                                                                         \
    ))

#define LIX_RPC_FILL_STRUCT(fobj, ffield, fsource, ...)                    \
    (::nix::rpc::detail::doFill(                                           \
        fobj,                                                              \
        ([]<typename Builder, typename Inner>(Inner (Builder::*field)()) { \
            return field;                                                  \
        })(&decltype(fobj)::ffield),                                       \
        (fsource),                                                         \
        ##__VA_ARGS__                                                      \
    ))

// set a value regardless of its kind. *only* use this in templates or it'll fail to compile.
#define LIX_RPC_FILL_GENERIC_DEPENDENT(fobj, ffield, fsource, ...)                              \
    ({                                                                                          \
        if constexpr (requires {                                                                \
                          requires ::nix::rpc::detail::RpcList<                                 \
                              typename decltype(fobj.get##ffield())::Builds>;                   \
                      })                                                                        \
        {                                                                                       \
            LIX_RPC_FILL_LIST(fobj, init##ffield, (fsource), ##__VA_ARGS__);                    \
        } else if constexpr (requires {                                                         \
                                 { LIX_RPC_FILL(fobj, set##ffield, (fsource), ##__VA_ARGS__) }; \
                             })                                                                 \
        {                                                                                       \
            LIX_RPC_FILL(fobj, set##ffield, (fsource), ##__VA_ARGS__);                          \
        } else {                                                                                \
            LIX_RPC_FILL_STRUCT(fobj, init##ffield, (fsource), ##__VA_ARGS__);                  \
        }                                                                                       \
    })

namespace detail {
std::exception_ptr unwrapErrorRaw(kj::Exception & e, std::source_location loc);
std::exception_ptr unwrapErrorV1(kj::Exception & e, std::source_location loc);

[[noreturn]]
void rethrowAsErrorV1();

kj::Promise<nix::Result<void>> inline rewrapNoexcept(
    kj::Promise<void> && promise, std::source_location loc = std::source_location::current()
)
{
    return promise.then(
        []() -> nix::Result<void> { return result::success(); },
        [loc](kj::Exception && e) -> nix::Result<void> { return result::failure(unwrapErrorRaw(e, loc)); }
    );
}

template<typename T>
kj::Promise<nix::Result<T>>
rewrapNoexcept(kj::Promise<T> && promise, std::source_location loc = std::source_location::current())
{
    return promise.then(
        [](T result) -> nix::Result<T> { return result; },
        [loc](kj::Exception && e) -> nix::Result<T> { return result::failure(unwrapErrorRaw(e, loc)); }
    );
}

kj::Promise<nix::Result<void>> inline rewrapV1(
    kj::Promise<void> && promise, std::source_location loc = std::source_location::current()
)
{
    return promise.then(
        []() -> nix::Result<void> { return result::success(); },
        [loc](kj::Exception && e) -> nix::Result<void> { return result::failure(unwrapErrorV1(e, loc)); }
    );
}

template<typename T>
kj::Promise<nix::Result<T>>
rewrapV1(kj::Promise<T> && promise, std::source_location loc = std::source_location::current())
{
    return promise.then(
        [](T result) -> nix::Result<T> { return result; },
        [loc](kj::Exception && e) -> nix::Result<T> { return result::failure(unwrapErrorV1(e, loc)); }
    );
}

// we can't use the logger header here because this header is also used from libexec helpers
kj::Promise<void> flushLogger();

static kj::Promise<void> wrapForRpcV1(auto inner)
try {
    std::exception_ptr error;
    try {
        LIX_TRY_AWAIT(inner());
    } catch (...) {
        error = std::current_exception();
    }
    // ignore flush errors as we can't handle them.
    // TODO maybe terminate the connection instead?
    co_await flushLogger();
    if (error) {
        std::rethrow_exception(error);
    }
} catch (...) {
    rethrowAsErrorV1();
}
}

#define LIX_WRAP_RPC_PROMISE_NOEXCEPT(...) (::nix::rpc::detail::rewrapNoexcept(__VA_ARGS__))
#define LIX_WRAP_RPC_PROMISE_V1(...) (::nix::rpc::detail::rewrapV1(__VA_ARGS__))

// IMPORTANT! Keep the result of this in a variable, or readers will dereference dangling pointers!
#define LIX_TRY_AWAIT_RPC_NOEXCEPT(...) (LIX_TRY_AWAIT(LIX_WRAP_RPC_PROMISE_NOEXCEPT(__VA_ARGS__)))
// IMPORTANT! Keep the result of this in a variable, or readers will dereference dangling pointers!
#define LIX_TRY_AWAIT_RPC_V1(...) (LIX_TRY_AWAIT(LIX_WRAP_RPC_PROMISE_V1(__VA_ARGS__)))

#define LIX_RPC_IMPL_V1(...)                                                                  \
    /* NOLINTBEGIN(cppcoreguidelines-avoid-capturing-lambda-coroutines):                      \
     * the wrapForRpc functions keep the closure alive for us as needed.                      \
     */                                                                                       \
    ::nix::rpc::detail::wrapForRpcV1([context, this]() mutable -> kj::Promise<Result<void>> { \
        /* NOLINTEND(cppcoreguidelines-avoid-capturing-lambda-coroutines) */                  \
        try {                                                                                 \
            (void) (this, context);                                                           \
            __VA_ARGS__;                                                                      \
            co_return result::success();                                                      \
        } catch (...) {                                                                       \
            co_return result::current_exception();                                            \
        }                                                                                     \
    })

[[noreturn]]
inline void rethrow_as_rpc_error()
{
    detail::rethrowAsErrorV1();
}

#ifdef LIX_UR_COMPILER_UWU
#define RPC_FILL LIX_RPC_FILL
#define RPC_FILL_LIST LIX_RPC_FILL_LIST
#define RPC_FILL_STRUCT LIX_RPC_FILL_STRUCT
#define TRY_AWAIT_RPC_NOEXCEPT LIX_TRY_AWAIT_RPC_NOEXCEPT
#define TRY_AWAIT_RPC_V1 LIX_TRY_AWAIT_RPC_V1
#define TRY_AWAIT_RPC LIX_TRY_AWAIT_RPC_V1
#define RPC_IMPL LIX_RPC_IMPL_V1
#endif
}
