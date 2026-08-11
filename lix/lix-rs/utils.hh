#pragma once
///@file convenience utilities for working with the rust ffi bits

#include <atomic>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <exception>
#include <kj/async.h>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>
#include <variant>
#include <set>
#include <list>

// this header requires `std` to mean `::std`
#include "lix/lix-rs/zngur.gen.hh"

#include "lix/libutil/result.hh"

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

namespace std::ffi {
struct OsStr;
}

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

namespace std::collections::hash_set {
template<typename...>
struct HashSet;
}

namespace rootcause {
struct Report;
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
        return typename Result<V, Err>::Ok(::std::move(v));
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
        return typename Result<Ok, V>::Err(::std::move(v));
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
::std::string_view to_std_string_view(Ref<String> s);
::std::string to_std_string(Ref<String> s);

namespace std::string {
::std::string_view to_std_string_view(const String & s);
::std::string to_std_string(const String & s);
}

namespace std::ffi {
struct OsStr;
struct OsString;
}

::std::string_view to_std_string_view(Ref<std::ffi::OsStr> s);
::std::string to_std_string(Ref<std::ffi::OsStr> s);

::std::string_view to_std_string_view(Ref<std::ffi::OsString> s);
::std::string to_std_string(Ref<std::ffi::OsString> s);

std::collections::hash_set::HashSet<String> to_hash_set(const ::std::set<::std::string> & s);
std::vec::Vec<String> to_vec(const ::std::vector<::std::string> & s);
std::vec::Vec<String> to_vec(const ::std::list<::std::string> & s);

String to_string(::std::string_view sv);

namespace lix::ffi {
Ref<std::ffi::OsStr> to_os_str(const uint8_t * raw, size_t size) noexcept;
}

Ref<std::ffi::OsStr> to_os_str(::std::string_view sv);

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

    if (Result<Args...>::Ok::check(r)) {
        return result_type(match<matches::Ok, decltype(r.unwrap())>{r.unwrap()});
    } else {
        return result_type(match<matches::Err, decltype(r.unwrap_err())>{r.unwrap_err()});
    }
}

template<typename Ok, typename Err, ::std::invocable<Ok> FnOk, ::std::invocable<Err> FnErr>
auto match_result(Result<Ok, Err> r, FnOk ok, FnErr err)
    -> ::std::common_type_t<::std::invoke_result_t<FnOk, Ok>, ::std::invoke_result_t<FnErr, Err>>
{
    return Result<Ok, Err>::Ok::check(r) ? ok(r.unwrap()) : err(r.unwrap_err());
}

namespace detail {
[[noreturn]]
void throw_from_report(rootcause::Report & r);
}

template<typename Ok>
Ok unwrap(Result<Ok, rootcause::Report> result)
{
    return match_result(
        ::std::move(result),
        [](auto ok) { return ok; },
        [](auto err) -> Ok { detail::throw_from_report(err); }
    );
}
}

namespace std::option {
template<typename... Args>
auto to_std(Option<Args...> r)
{
    using result_type = ::std::optional<decltype(r.unwrap())>;

    if (Option<Args...>::Some::check(r)) {
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

namespace rust {
template<typename Lhs, typename Rhs>
concept HasOpLt = requires(Lhs lhs, Rhs rhs) {
    lhs.lt(rhs);
    Ref<::std::remove_cvref_t<Lhs>>{};
    Ref<::std::remove_cvref_t<Rhs>>{};
};

template<typename Lhs, typename Rhs>
concept HasOpLe = requires(Lhs lhs, Rhs rhs) {
    lhs.le(rhs);
    Ref<::std::remove_cvref_t<Lhs>>{};
    Ref<::std::remove_cvref_t<Rhs>>{};
};

template<typename Lhs, typename Rhs>
concept HasOpEq = requires(Lhs lhs, Rhs rhs) {
    lhs.eq(rhs);
    Ref<::std::remove_cvref_t<Lhs>>{};
    Ref<::std::remove_cvref_t<Rhs>>{};
};

template<typename T>
struct OptionArg;
template<typename T>
struct OptionArg<Option<T>>
{
    using type = T;
};

template<typename T>
concept IsOption = requires { typename OptionArg<T>::type; };

template<typename T>
concept Iterator = requires(T t) {
    { t.next() } -> IsOption;
};

namespace detail {
struct iterator_end
{};

template<typename Iter>
class iterator
{
    using step_type = decltype(to_std(::std::declval<Iter>().next()));

    Iter rs;
    step_type current;

public:
    using iterator_category = ::std::input_iterator_tag;
    using difference_type = void;
    using value_type = step_type::value_type;
    using reference = value_type &;
    using pointer = value_type *;

    explicit iterator(Iter rs) : rs(::std::move(rs))
    {
        ++*this;
    }

    pointer operator->()
    {
        return &*current;
    }
    reference operator*()
    {
        return *current;
    }

    iterator & operator++()
    {
        current = to_std(rs.next());
        return *this;
    }

    void operator++(int)
    {
        ++*this;
    }

    bool operator==(iterator_end) const
    {
        return !current.has_value();
    }
};
}

namespace lix::errors {
rootcause::Report report_from_string_unhooked(String) noexcept;
rootcause::Report current_exception_as_report();
}

namespace lix::futures {
class Waker
{
    kj::Own<const kj::Executor> executor = kj::getCurrentThreadExecutor().addRef();
    // SAFETY: only `executor` ever touches this member after construction
    mutable kj::Own<kj::PromiseFulfiller<void>> fulfiller;
    mutable ::std::atomic<size_t> refs = 1;

    Waker(kj::Own<kj::PromiseFulfiller<void>> fulfiller) : fulfiller(::std::move(fulfiller)) {}

public:
    // if kj destructors throw we're going to be in *so* much trouble here 🙃
    ~Waker() noexcept(true) = default;

    static auto build(kj::Own<kj::PromiseFulfiller<void>> fulfiller)
    {
        using Deleter = decltype([](Waker * w) { w->dropRef(); });
        return ::std::unique_ptr<Waker, Deleter>{new Waker(::std::move(fulfiller))};
    }

    void wake() const noexcept;
    void addRef() const noexcept;
    void dropRef() const noexcept;
};

template<typename... R>
struct RsFuture;
template<typename...>
struct CxxFuture;
template<typename...>
struct CxxFutureState;
struct CxxPromise;

template<typename R>
kj::Promise<R> to_kj(rust::lix::futures::RsFuture<R> f)
{
    // kj throws when cleaning up an event loop that has "events still in the queue",
    // which should be impossible when all promises have been properly destroyed. the
    // kj coroutine implementation seems to have some corner cases that make them not
    // entirely suitable for this implementation. this *must* be a continuation chain
    // to work, if it's not we run into aforementioned exception during loop shutdown
    // if other threads are signalling any of our wakers during that time. we are not
    // sure why this happens, but it's likely that internal kj state is being cleaned
    // up by creating new events that themselves require processing by the event loop

    auto paf = kj::newPromiseAndFulfiller<void>();
    auto waker = futures::Waker::build(::std::move(paf.fulfiller));
    if (auto r = to_std(f.poll(*waker))) {
        return {::std::move(*r)};
    } else {
        return paf.promise.then([f = ::std::move(f)] mutable { return to_kj(::std::move(f)); });
    }
}

template<typename R>
CxxFuture<R> to_rust(kj::Promise<::nix::Result<R>> p)
{
    // anything in this function that looks like it's completely pointless and
    // makes no sense is there precisely to delay c++ type checks of the code.
    // we don't have definitions for most of the things we are using here, and
    // anything that is templated in some way delays type checking to template
    // instantiation time. it also delays *binding* to instantiation time, and
    // in doing so lets us use methods we *cannot even know of* at this point.
    using promise_t = ::std::enable_if_t<((void) sizeof(p), true), CxxPromise>;
    using res_t = Result<R, rootcause::Report>;
    using to_error_t = decltype([](auto arg) { return errors::report_from_string_unhooked(to_string(arg)); });

    auto state = CxxFutureState<R>::new_();
    auto resolve = [f = state.add_ref()](auto result) {
        try {
            f.resolve(typename res_t::Ok(::std::move(result.value())));
        } catch (...) {
            f.resolve(typename res_t::Err([](auto f) { return f(); }(errors::current_exception_as_report)));
        }
    };
    auto fail = [f = state.add_ref()](kj::Exception && e) {
        f.resolve(typename res_t::Err(to_error_t()(e.getDescription().cStr())));
    };

    return CxxFuture<R>::new_(
        promise_t::build(p.then(::std::move(resolve)).eagerlyEvaluate(::std::move(fail))), ::std::move(state)
    );
}

template<typename R>
CxxFuture<R> to_rust(kj::Promise<R> p)
{
    return to_rust(p.then([](auto r) -> ::nix::Result<R> { return ::nix::result::success(::std::move(r)); }));
}
}
}

// clang-format off
#define LIX_DECLARE_ORD_OPS(ns)                                                         \
    namespace ns {                                                                      \
        template<typename Lhs, typename Rhs> requires ::rust::HasOpLt<Lhs, Rhs>         \
        bool operator<(const Lhs & lhs, const Rhs & rhs) { return bool(lhs.lt(rhs)); }  \
        template<typename Lhs, typename Rhs> requires ::rust::HasOpLe<Lhs, Rhs>         \
        bool operator<=(const Lhs & lhs, const Rhs & rhs) { return bool(lhs.le(rhs)); } \
        template<typename Lhs, typename Rhs> requires ::rust::HasOpLt<Lhs, Rhs>         \
        bool operator>(const Lhs & lhs, const Rhs & rhs) { return bool(rhs.lt(lhs)); }  \
        template<typename Lhs, typename Rhs> requires ::rust::HasOpLe<Lhs, Rhs>         \
        bool operator>=(const Lhs & lhs, const Rhs & rhs) { return bool(rhs.le(lhs)); } \
    }
#define LIX_DECLARE_EQ_OPS(ns)                                                           \
    namespace ns {                                                                       \
        template<typename Lhs, typename Rhs> requires ::rust::HasOpEq<Lhs, Rhs>          \
        bool operator==(const Lhs & lhs, const Rhs & rhs) { return bool(lhs.eq(rhs)); }  \
        template<typename Lhs, typename Rhs> requires ::rust::HasOpEq<Lhs, Rhs>          \
        bool operator!=(const Lhs & lhs, const Rhs & rhs) { return !bool(lhs.eq(rhs)); } \
    }
#define LIX_DECLARE_ITERATORS(ns) \
    namespace ns { \
        template<::rust::Iterator T> \
        auto begin(T & rs) { return ::rust::detail::iterator{::std::move(rs)}; } \
        template<::rust::Iterator T> \
        auto end(const T & rs) { return ::rust::detail::iterator_end{}; } \
    }
// clang-format on

LIX_DECLARE_ORD_OPS(rust::lix::ffi_test)
LIX_DECLARE_EQ_OPS(rust::lix::ffi_test)

LIX_DECLARE_ITERATORS(rust::std::slice)
LIX_DECLARE_ITERATORS(rust::std::vec)
LIX_DECLARE_ITERATORS(rust::std::collections::hash_set)
