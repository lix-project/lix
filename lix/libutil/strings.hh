#pragma once
///@file

#include "lix/libutil/error.hh"
#include "lix/libutil/types.hh"

#include <vector>
#include <boost/container/small_vector.hpp>

namespace nix {

/**
 * Tree formatting.
 */
constexpr char treeConn[] = "├───";
constexpr char treeLast[] = "└───";
constexpr char treeLine[] = "│   ";
constexpr char treeNull[] = "    ";

/**
 * Convert a list of strings to a null-terminated vector of `char
 * *`s. The result must not be accessed beyond the lifetime of the
 * list of strings.
 *
 * Modifying the resulting array elements violates the constness of ss.
 */
std::vector<char *> stringsToCharPtrs(const Strings & ss);


MakeError(FormatError, Error);


/**
 * String tokenizer.
 */
template<class C> C tokenizeString(std::string_view s, std::string_view separators = " \t\n\r");

/**
 * Concatenate the given strings with a separator between the
 * elements.
 */
template<class C>
std::string concatStringsSep(const std::string_view sep, const C & ss)
{
    size_t size = 0;
    // need a cast to string_view since this is also called with Symbols
    for (const auto & s : ss) size += sep.size() + std::string_view(s).size();
    std::string s;
    s.reserve(size);
    for (auto & i : ss) {
        if (s.size() != 0) s += sep;
        s += i;
    }
    return s;
}

template<class ... Parts>
auto concatStrings(Parts && ... parts)
    -> std::enable_if_t<(... && std::is_convertible_v<Parts, std::string_view>), std::string>
{
    std::string_view views[sizeof...(parts)] = { parts... };
    return concatStringsSep({}, views);
}


/**
 * Apply a function to the `iterable`'s items and concat them with `separator`.
 */
template<class C, class F>
std::string concatMapStringsSep(std::string_view separator, const C & iterable, F fn)
{
    boost::container::small_vector<std::string, 64> strings;
    strings.reserve(iterable.size());
    for (const auto & elem : iterable) {
        strings.push_back(fn(elem));
    }
    return concatStringsSep(separator, strings);
}



/**
 * Add quotes around a collection of strings.
 */
template<class C> Strings quoteStrings(const C & c)
{
    Strings res;
    for (auto & s : c)
        res.push_back("'" + s + "'");
    return res;
}

/**
 * Remove trailing whitespace from a string.
 *
 * \todo return std::string_view.
 */
std::string chomp(std::string_view s);


/**
 * Remove whitespace from the start and end of a string.
 */
std::string trim(std::string_view s, std::string_view whitespace = " \n\r\t");


/**
 * Replace all occurrences of a string inside another string.
 */
std::string replaceStrings(
    std::string s,
    std::string_view from,
    std::string_view to);


/**
 * Rewrites a string given a map of replacements, applying the replacements in
 * sorted order, only once, considering only the strings appearing in the input
 * string in performing replacement.
 *
 * - Replacements are not performed on intermediate strings. That is, for an input
 *   `"abb"` with replacements `{"ab" -> "ba"}`, the result is `"bab"`.
 * - Transitive replacements are not performed. For example, for the input `"abcde"`
 *   with replacements `{"a" -> "b", "b" -> "c", "e" -> "b"}`, the result is
 *   `"bccdb"`.
 */
class Rewriter
{
private:
    std::string initials;
    std::map<std::string, std::string> rewrites;

public:
    explicit Rewriter(std::map<std::string, std::string> rewrites);

    std::string operator()(std::string s);
};

inline std::string rewriteStrings(std::string s, const StringMap & rewrites)
{
    return Rewriter(rewrites)(s);
}



/**
 * Parse a string into an integer.
 */
template<class N>
std::optional<N> string2Int(const std::string_view s);

/**
 * Like string2Int(), but support an optional suffix 'K', 'M', 'G' or
 * 'T' denoting a binary unit prefix.
 */
template<class N>
N string2IntWithUnitPrefix(std::string_view s)
{
    N multiplier = 1;
    if (!s.empty()) {
        char u = std::toupper(*s.rbegin());
        if (std::isalpha(u)) {
            if (u == 'K') multiplier = 1ULL << 10;
            else if (u == 'M') multiplier = 1ULL << 20;
            else if (u == 'G') multiplier = 1ULL << 30;
            else if (u == 'T') multiplier = 1ULL << 40;
            else throw UsageError("invalid unit specifier '%1%'", u);
            s.remove_suffix(1);
        }
    }
    if (auto n = string2Int<N>(s))
        return *n * multiplier;
    throw UsageError("'%s' is not an integer", s);
}

/**
 * Parse a string into a float.
 */
template<class N>
std::optional<N> string2Float(const std::string_view s);


/**
 * Convert a string to lower case.
 */
std::string toLower(const std::string & s);


/**
 * Escape a string as a shell word.
 */
std::string shellEscape(const std::string_view s);

/**
 * Same as shellEscape, but also escapes nonprinting characters using $'ANSI C quotes'.
 */
std::string bashEscape(const std::string_view s);

/**
 * Base64 encoding/decoding.
 */
std::string base64Encode(std::string_view s);
std::string base64Decode(std::string_view s);


/**
 * Remove common leading whitespace from the lines in the string
 * 's'. For example, if every line is indented by at least 3 spaces,
 * then we remove 3 spaces from the start of every line.
 */
std::string stripIndentation(std::string_view s);

/**
 * Get the prefix of 's' up to and excluding the next line break (LF
 * optionally preceded by CR), and the remainder following the line
 * break.
 */
std::pair<std::string_view, std::string_view> getLine(std::string_view s);

std::string showBytes(uint64_t bytes);


/**
 * Provide an addition operator between `std::string` and `std::string_view`
 * inexplicably omitted from the standard library.
 *
 * > The reason for this is given in n3512 string_ref: a non-owning reference
 * to a string, revision 2 by Jeffrey Yasskin:
 * >
 * > > I also omitted operator+(basic_string, basic_string_ref) because LLVM
 * > > returns a lightweight object from this overload and only performs the
 * > > concatenation lazily. If we define this overload, we'll have a hard time
 * > > introducing that lightweight concatenation later.
 *
 * See: https://stackoverflow.com/a/47735624
 */
inline std::string operator + (const std::string & s1, std::string_view s2)
{
    auto s = s1;
    s.append(s2);
    return s;
}

inline std::string operator + (std::string && s, std::string_view s2)
{
    s.append(s2);
    return std::move(s);
}

inline std::string operator + (std::string_view s1, const char * s2)
{
    std::string s;
    s.reserve(s1.size() + strlen(s2));
    s.append(s1);
    s.append(s2);
    return s;
}

}
