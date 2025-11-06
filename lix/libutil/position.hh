#pragma once
/**
 * @file
 *
 * @brief Pos and AbstractPos
 */

#include <cstdint>
#include <string>

#include "lix/libutil/source-path.hh"

namespace nix {

/**
 * A position and an origin for that position (like a source file).
 */
struct Pos
{
    uint32_t line = 0;
    uint32_t column = 0;

    struct Stdin {
        ref<std::string> source;
        constexpr friend auto operator<=>(Stdin const & lhs, Stdin const & rhs)
        {
            return *lhs.source <=> *rhs.source;
        }
    };
    struct String {
        ref<std::string> source;
        constexpr friend auto operator<=>(String const & lhs, String const & rhs)
        {
            return *lhs.source <=> *rhs.source;
        }
    };
    struct Hidden {
        auto operator<=>(const Hidden &) const = default;
    };

    struct Origin : std::variant<std::monostate, Stdin, String, CheckedSourcePath, Hidden>
    {
        using variant = variant;

        // Forward all construction to std::variant.
        template<typename... Args>
        constexpr Origin(Args &&... rhs) noexcept(noexcept(variant(std::forward<Args>(rhs)...)))
            : variant(std::forward<Args>(rhs)...)
        {
        }

        std::optional<std::string> getSource() const;
    };

    Origin origin = std::monostate();

    Pos() { }
    Pos(uint32_t line, uint32_t column, Origin origin)
        : line(line), column(column), origin(origin) { }
    Pos(Pos & other) = default;
    Pos(const Pos & other) = default;
    Pos(Pos && other) = default;
    Pos(const Pos * other);

    explicit operator bool() const { return line > 0; }

    operator std::shared_ptr<Pos>() const;

    /**
     * Return the contents of the source file.
     */
    std::optional<std::string> getSource() const;

    void print(std::ostream & out, bool showOrigin) const;

    std::optional<LinesOfCode> getCodeLines() const;

    // Not defaulted because it's implicitly deleted because C++ is stupid.
    constexpr friend auto operator<=>(Pos const & lhs, Pos const & rhs)
    {
        return std::forward_as_tuple(lhs.line, lhs.column, lhs.origin)
            <=> std::forward_as_tuple(rhs.line, rhs.column, rhs.origin);
    }

    // operator<=> doesn't generate this because it's not defaulted.
    constexpr friend bool operator==(Pos const & lhs, Pos const & rhs) = default;

    struct LinesIterator {
        using difference_type = size_t;
        using value_type = std::string_view;
        using reference = const std::string_view &;
        using pointer = const std::string_view *;
        using iterator_category = std::input_iterator_tag;

        LinesIterator(): pastEnd(true) {}
        explicit LinesIterator(std::string_view input): input(input), pastEnd(input.empty()) {
            if (!pastEnd)
                bump(true);
        }

        LinesIterator & operator++() {
            bump(false);
            return *this;
        }
        LinesIterator operator++(int) {
            auto result = *this;
            ++*this;
            return result;
        }

        reference operator*() const { return curLine; }
        pointer operator->() const { return &curLine; }

        bool operator!=(const LinesIterator & other) const {
            return !(*this == other);
        }
        bool operator==(const LinesIterator & other) const {
            return (pastEnd && other.pastEnd)
                || (std::forward_as_tuple(input.size(), input.data())
                    == std::forward_as_tuple(other.input.size(), other.input.data()));
        }

    private:
        std::string_view input, curLine;
        bool pastEnd = false;

        void bump(bool atFirst);
    };
};

std::ostream & operator<<(std::ostream & str, const Pos & pos);

}
