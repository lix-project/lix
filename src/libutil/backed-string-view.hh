#pragma once
/// @file String view that can be either owned or borrowed.
#include <variant>
#include <string>
#include <string_view>

/**
 * This wants to be a little bit like rust's Cow type.
 * Some parts of the evaluator benefit greatly from being able to reuse
 * existing allocations for strings, but have to be able to also use
 * newly allocated storage for values.
 *
 * We do not define implicit conversions, even with ref qualifiers,
 * since those can easily become ambiguous to the reader and can degrade
 * into copying behaviour we want to avoid.
 */
class BackedStringView {
private:
    std::variant<std::string, std::string_view> data;

    /**
     * Needed to introduce a temporary since operator-> must return
     * a pointer. Without this we'd need to store the view object
     * even when we already own a string.
     */
    class Ptr {
    private:
        std::string_view view;
    public:
        Ptr(std::string_view view): view(view) {}
        const std::string_view * operator->() const { return &view; }
    };

public:
    BackedStringView(std::string && s): data(std::move(s)) {}
    BackedStringView(std::string_view sv): data(sv) {}
    template<size_t N>
    BackedStringView(const char (& lit)[N]): data(std::string_view(lit)) {}

    BackedStringView(const BackedStringView &) = delete;
    BackedStringView & operator=(const BackedStringView &) = delete;

    /**
     * We only want move operations defined since the sole purpose of
     * this type is to avoid copies.
     */
    BackedStringView(BackedStringView && other) = default;
    BackedStringView & operator=(BackedStringView && other) = default;

    bool isOwned() const
    {
        return std::holds_alternative<std::string>(data);
    }

    std::string toOwned() &&
    {
        return isOwned()
            ? std::move(std::get<std::string>(data))
            : std::string(std::get<std::string_view>(data));
    }

    std::string_view operator*() const
    {
        return isOwned()
            ? std::get<std::string>(data)
            : std::get<std::string_view>(data);
    }
    Ptr operator->() const { return Ptr(**this); }
};
