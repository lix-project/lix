#pragma once
///@file RPC helper functions for `types.hh`

#include "error.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/types.capnp.h"
#include "rpc.hh"
#include <capnp/any.h>
#include <capnp/common.h>
#include <cstdint>
#include <exception>
#include <ranges>
#include <type_traits>

namespace nix::rpc {

// ensure that verbosity levels match to make conversions trivial below.
static_assert(int(Verbosity::ERROR) == int(nix::Verbosity::lvlError));
static_assert(int(Verbosity::WARN) == int(nix::Verbosity::lvlWarn));
static_assert(int(Verbosity::NOTICE) == int(nix::Verbosity::lvlNotice));
static_assert(int(Verbosity::INFO) == int(nix::Verbosity::lvlInfo));
static_assert(int(Verbosity::TALKATIVE) == int(nix::Verbosity::lvlTalkative));
static_assert(int(Verbosity::CHATTY) == int(nix::Verbosity::lvlChatty));
static_assert(int(Verbosity::DEBUG) == int(nix::Verbosity::lvlDebug));
static_assert(int(Verbosity::VOMIT) == int(nix::Verbosity::lvlVomit));

inline nix::ErrorInfo from(const Error::Reader & e, auto &&... args)
{
    ErrorInfo ei{
        // capnp enums are u16, and we have checked that the enumerator values match
        .level = nix::Verbosity(std::clamp<std::underlying_type_t<nix::Verbosity>>(
            uint16_t(e.getLevel()), lvlError, lvlVomit
        )),
        .msg = HintFmt(to<std::string>(e.getMessage())),
    };
    for (auto && t : e.getTraces()) {
        ei.traces.push_back(Trace{.hint = HintFmt(to<std::string>(t, args...))});
    }
    return ei;
}

template<>
struct Fill<Error, nix::ErrorInfo>
{
    static void fill(Error::Builder eb, const nix::ErrorInfo & e, auto &&...)
    {
        eb.setLevel(Verbosity(e.level));
        LIX_RPC_FILL(eb, setMessage, e.msg.str());
        LIX_RPC_FILL(eb, initTraces, e.traces | std::ranges::views::transform([](auto & trace) {
                                         return trace.hint.str();
                                     }));
    }
};

namespace detail {
inline void makeBadResult(auto rb, const std::exception_ptr & e)
{
    try {
        std::rethrow_exception(e);
    } catch (nix::Error & e) {
        LIX_RPC_FILL(rb, initBad, e.info());
    } catch (std::exception & e) { // NOLINT(lix-foreign-exceptions)
        LIX_RPC_FILL(rb, initBad, nix::Error("caught non-lix exception: %s", e.what()).info());
    } catch (...) {
        LIX_RPC_FILL(rb, initBad, nix::Error("caught non-exception! spooky").info());
    }
}

template<typename>
inline constexpr bool IsResult = false;
template<typename T>
inline constexpr bool IsResult<Result<T>> = true;

template<typename T>
concept ResultReader = IsResult<typename T::Reads>;

template<typename T>
concept ResultBuilder = IsResult<typename T::Builds>;
}

inline nix::Result<void> from(const ResultV::Reader & r, auto &&... args)
{
    if (r.isGood()) {
        return result::success();
    } else {
        return result::failure(nix::Error(from(r.getBad(), args...)));
    }
}

template<>
struct Fill<ResultV, nix::Result<void>>
{
    static void fill(ResultV::Builder rb, const nix::Result<void> & r, auto &&...)
    {
        if (r.has_value()) {
            rb.setGood();
        } else {
            detail::makeBadResult(rb, r.error());
        }
    }
};

template<>
struct Fill<ResultV, std::exception_ptr>
{
    static void fill(ResultV::Builder rb, const std::exception_ptr & e, auto &&...)
    {
        detail::makeBadResult(rb, e);
    }
};

inline auto from(detail::ResultReader auto r, auto &&... args)
{
    using R = nix::Result<decltype(r.getGood())>;
    if (r.isGood()) {
        return R(result::success(r.getGood()));
    } else {
        return R(result::failure(nix::Error(from(r.getBad(), args...))));
    }
}

template<typename T>
struct Fill<Result<T>, std::exception_ptr>
{
    static void fill(Result<T>::Builder rb, const std::exception_ptr & e, auto &&...)
    {
        detail::makeBadResult(rb, e);
    }
};
}
