#pragma once
///@file

#include "lix/libutil/fmt.hh"
#include "lix/libutil/json-fwd.hh"

#include <cassert>
#include <format>
#include <optional>
#include <string_view>

namespace nix
{

/**
 * This was previously defined in libcmd, because libutil doesn't know what the hell a progress bar is.
 * But we want this to be a setting, and all the other logging stuff is here in libutil.
 */
struct LogFormat
{
    enum class LogFormatValue : uint8_t
    {
        Auto,
        Raw,
        RawWithLogs,
        InternalJson,
        Bar,
        BarWithLogs,
        Multiline,
        MultilineWithLogs,
    };

    using enum LogFormatValue;

public:
    LogFormatValue value;

    // Boilerplate.
public:
    constexpr LogFormat() noexcept : value(Auto) { }
    // Intentionally implicit constructor. For once.
    constexpr LogFormat(LogFormatValue const & value) noexcept : value(value) { }

    // Intentionally implicit operator. For once.
    constexpr operator LogFormatValue() const noexcept
    {
        return value;
    }

    static constexpr std::optional<LogFormatValue> parse(std::string_view str)
    {
        if (str == "auto") {
            return Auto;
        } else if (str == "raw") {
            return Raw;
        } else if (str == "raw-with-logs") {
            return RawWithLogs;
        } else if (str == "internal-json") {
            return InternalJson;
        } else if (str == "bar") {
            return Bar;
        } else if (str == "bar-with-logs") {
            return BarWithLogs;
        } else if (str == "multiline") {
            return Multiline;
        } else if (str == "multiline-with-logs") {
            return MultilineWithLogs;
        }

        return std::nullopt;
    }

    constexpr std::string_view toStr() const noexcept
    {
        switch (value) {
        case Auto:
            return "auto";
        case Raw:
            return "raw";
        case RawWithLogs:
            return "raw-with-logs";
        case InternalJson:
            return "internal-json";
        case Bar:
            return "bar";
        case BarWithLogs:
            return "bar-with-logs";
        case Multiline:
            return "multiline";
        case MultilineWithLogs:
            return "multiline-with-logs";
        }
    }

    // Actual API.
public:
    /** Does nothing if not applicable.
     *
    */
    constexpr LogFormatValue withoutLogs() const noexcept
    {
        switch (value) {
        case Auto:
            [[fallthrough]];
        case Raw:
            [[fallthrough]];
        case Bar:
            [[fallthrough]];
        case Multiline:
            [[fallthrough]];
        case InternalJson:
            return *this;

        case RawWithLogs:
            return Raw;
        case BarWithLogs:
            return Bar;
        case MultilineWithLogs:
            return Multiline;
        }
    }

    constexpr LogFormatValue withLogs() const noexcept
    {
        switch (value) {
        case Auto:
            [[fallthrough]];
        case RawWithLogs:
            [[fallthrough]];
        case BarWithLogs:
            [[fallthrough]];
        case MultilineWithLogs:
            [[fallthrough]];
        case InternalJson:
            return *this;

        case Raw:
            return RawWithLogs;
        case Bar:
            return BarWithLogs;
        case Multiline:
            return MultilineWithLogs;
        }
    }
};

using LogFormatValue = LogFormat::LogFormatValue;

template<>
struct LegacyFormat<LogFormat> : std::true_type { };
template<>
struct LegacyFormat<LogFormatValue> : std::true_type { };

template<>
struct json::is_integral_enum<nix::LogFormat> : std::true_type {};
template<>
struct json::is_integral_enum<nix::LogFormatValue> : std::true_type {};

}

template<>
struct std::formatter<nix::LogFormat> : std::formatter<std::string_view>
{
    template<typename Ctx>
    constexpr Ctx::iterator format(nix::LogFormat self, Ctx & ctx) const
    {
        return std::formatter<std::string_view>::format(self.toStr(), ctx);
    }
};

template<>
struct std::formatter<nix::LogFormatValue> : std::formatter<nix::LogFormat>
{
    template<typename Ctx>
    constexpr Ctx::iterator format(nix::LogFormatValue self, Ctx & ctx) const
    {
        return std::formatter<nix::LogFormatValue>::format(self, ctx);
    }
};
