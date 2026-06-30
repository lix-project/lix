#pragma once
///@file RPC helper functions for `types.hh`

#include "error.hh"
#include "lix/libutil/config.hh"
#include "lix/libutil/result.hh"
#include "lix/libutil/types.capnp.h"
#include "rpc.hh"
#include <capnp/any.h>
#include <capnp/common.h>
#include <cstdint>
#include <exception>
#include <ranges>
#include <string>
#include <string_view>
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

template<>
struct Fill<Settings::Setting, std::pair<const std::string, Config::SettingInfo>>
{
    static void fill(
        Settings::Setting::Builder sb,
        const std::pair<const std::string, Config::SettingInfo> & s,
        auto &&... args
    )
    {
        LIX_RPC_FILL(sb, setName, s.first);
        LIX_RPC_FILL(sb, setValue, s.second.value);
    }
};

template<>
struct Fill<Settings, std::map<std::string, Config::SettingInfo>>
{
    static void fill(
        Settings::Builder sb, const std::map<std::string, Config::SettingInfo> & s, auto &&... args
    )
    {
        LIX_RPC_FILL(sb, initMap, s);
    }
};

template<>
struct Convert<Settings, std::map<std::string, std::string>>
{
    static std::map<std::string, std::string> convert(const Settings::Reader & t, auto &&...)
    {
        std::map<std::string, std::string> result;
        for (const auto & s : t.getMap()) {
            result[rpc::to<std::string>(s.getName())] = rpc::to<std::string>(s.getValue());
        }
        return result;
    }
};

namespace error::v1 {
std::string encodeLossy(const ::nix::ErrorInfo & e);
std::optional<::nix::ErrorInfo> tryDecode(std::string_view source);
}
}
