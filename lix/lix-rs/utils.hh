#pragma once
///@file convenience utilities for working with the rust ffi bits

#include <cassert>
#include <exception>
#include <optional>
#include <string_view>
#include <type_traits>
#include <variant>

// this header requires `std` to mean `::std`
#include "lix/lix-rs/zngur.gen.hh"

// bunch of forward declarations to avoid including all other headers.
namespace rust {
template<typename...>
struct Box;
template<typename...>
struct Dyn;
template<typename...>
struct Fn;
template<typename>
struct Ref;

struct Str;

namespace std::option {
template<typename...>
struct Option;
}

namespace std::result {
template<typename...>
struct Result;
}

namespace std::string {
struct String;
}

namespace std::vec {
template<typename...>
struct Vec;
}

using std::option::Option;
using std::result::Result;
using std::string::String;
using std::vec::Vec;

namespace lix::ffi {
struct Error;
}
}

// here come the *actual* utilities
namespace rust {

/// marker that turns a value into an Ok result when returned
template<typename V>
struct as_ok
{
    V v;

    explicit as_ok(V v) : v(::std::move(v)) {}

    template<typename Err>
    operator Result<V, Err>()
    {
        return Result<V, Err>::Ok(::std::move(v));
    }
};

/// marker that turns a value into an Err result when returned
template<typename V>
struct as_err
{
    V v;

    explicit as_err(V v) : v(::std::move(v)) {}

    template<typename Ok>
    operator Result<Ok, V>()
    {
        return Result<Ok, V>::Err(::std::move(v));
    }
};

/// wrapper that turns a C++ function object into a `Box<dyn Fn...>`. the function objects
/// *must not* throw exceptions, otherwise the program will just abort. only use this when
/// you are absolutely certain that your function does not throw! if you aren't certain it
/// will throw. if you *are* certain it will throw anyway. passed callables must be marked
/// `noexcept` to serve as a reminder, if not marked the conversions will not be provided.
template<typename Func>
struct make_box_fn_noexcept
{
    Func fn;

    explicit make_box_fn_noexcept(Func fn) : fn(::std::move(fn)) {}

    template<typename R, typename... Args>
        requires requires(Func fn, Args... args) {
            { fn(::std::move(args)...) } noexcept -> ::std::same_as<R>;
        }
    operator Box<Dyn<Fn<R, Args...>>>() const
    {
        return Box<Dyn<Fn<R, Args...>>>::make_box(::std::move(fn));
    }
};

/// wrapper that turns a C++ function object into a `Box<dyn Fn...>`. the function objects
/// passed here may throw exceptions. to allow for this their return value is wrapped in a
/// `Result<T, lix::ffi::Error>` and exceptions are automatically turned into Err results.
template<typename Fn>
auto make_box_fn(Fn fn)
{
    return make_box_fn_noexcept([fn{::std::move(fn)}]<typename... Args>(Args &&... args) noexcept {
        using result_inner = decltype(fn(::std::forward<Args>(args)...));
        using result_type =
            Result<::std::conditional_t<::std::is_void_v<result_inner>, Unit, result_inner>, lix::ffi::Error>;
        try {
            if constexpr (::std::is_void_v<result_inner>) {
                fn(::std::forward<Args>(args)...);
                return result_type(as_ok(::rust::Unit()));
            } else {
                return result_type(as_ok(fn(::std::forward<Args>(args)...)));
            }
        } catch (...) {
            // make the error type dependent to delay type checking
            using error_type = ::std::enable_if_t<!::std::is_void_v<Fn>, lix::ffi::Error>;
            return result_type(as_err(error_type::build(::std::current_exception())));
        }
    });
}

::std::string_view to_std_string_view(Ref<Str> s);
::std::string to_std_string(Ref<Str> s);

namespace std::string {
::std::string_view to_std_string_view(const String & s);
::std::string to_std_string(const String & s);
}

String to_string(::std::string_view sv);

// enum type matching
namespace matches {
struct Ok;
struct Err;
}

template<typename Tag, typename T>
struct match
{
    T value;

    operator T()
    {
        return ::std::move(value);
    }
};

namespace std::result {
template<typename... Args>
auto to_std(Result<Args...> r)
{
    using result_type = ::std::
        variant<match<matches::Ok, decltype(r.unwrap())>, match<matches::Err, decltype(r.unwrap_err())>>;

    if (r.matches_Ok()) {
        return result_type(match<matches::Ok, decltype(r.unwrap())>{r.unwrap()});
    } else {
        return result_type(match<matches::Err, decltype(r.unwrap_err())>{r.unwrap_err()});
    }
}

template<typename Ok, typename Err, ::std::invocable<Ok> FnOk, ::std::invocable<Err> FnErr>
auto match_result(Result<Ok, Err> r, FnOk ok, FnErr err)
    -> ::std::common_type_t<::std::invoke_result_t<FnOk, Ok>, ::std::invoke_result_t<FnErr, Err>>
{
    return r.matches_Ok() ? ok(r.unwrap()) : err(r.unwrap_err());
}
}

namespace std::option {
template<typename... Args>
auto to_std(Option<Args...> r)
{
    using result_type = ::std::optional<decltype(r.unwrap())>;

    if (r.matches_Some()) {
        return result_type{r.unwrap()};
    } else {
        return result_type{};
    }
}
}
}

namespace nix {
using namespace rust::lix;
}
