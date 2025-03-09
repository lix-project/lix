#pragma once
///@file

#include <string_view>
#include <type_traits>
#include <utility>
#include <optional>
#include <ranges>

#include "lix/libutil/args.hh"

namespace nix::cli {
template<typename Enum>
struct enum_cli_traits;

template<typename Enum>
constexpr std::string_view toString(Enum value)
{
    for (const auto & [name, val] : enum_cli_traits<Enum>::values) {
        if (val == value) {
            return name;
        }
    }
    std::terminate();
}

template<typename Enum>
std::optional<Enum> fromString(std::string_view str)
{
    for (const auto & [name, val] : enum_cli_traits<Enum>::values) {
        if (name == str) {
            return val;
        }
    }
    return std::nullopt;
}

template<typename Enum>
void completeAmongEnumChoices(AddCompletions & completions, size_t, std::string_view prefix)
{
    for (const auto & [name, _] : enum_cli_traits<Enum>::values) {
        if (name.starts_with(prefix)) {
            completions.add(name);
        }
    }
}

template<typename Enum>
Enum parseEnumArg(std::string text)
{
    auto valueOpt = fromString<Enum>(text);

    if (valueOpt) {
        return *valueOpt;
    } else {
        auto names = std::ranges::views::keys(enum_cli_traits<Enum>::values)
            | std::ranges::to<std::set<std::string>>();
        auto suggestions = Suggestions::bestMatches(names, text);
        throw UsageError(suggestions, "'%s' is not a recognised '%s'", text, enum_cli_traits<Enum>::typeName);
    }
}

template<typename Enum>
std::optional<Enum> parseOptionalEnumArg(std::string text)
{
    auto target = fromString<Enum>(text);

    if (!target && text != "") {
        auto names = std::ranges::views::keys(enum_cli_traits<Enum>::values) | std::ranges::to<std::set>();
        auto suggestions = Suggestions::bestMatches(names, text);
        throw UsageError(suggestions, "'%s' is not a recognised '%s'", text, enum_cli_traits<Enum>::typeName);
    }

    return target;
}
}
