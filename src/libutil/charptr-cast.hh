#pragma once
/** @file Safe casts between character pointer types. */

#include <concepts> // IWYU pragma: keep
#include <type_traits>

namespace nix {

namespace charptr_cast_detail {

/** Custom version of std::decay that does not eat CV qualifiers on \c {char * const}. */
template<typename T>
struct DecayArrayInternal
{
    using type = T;
};

template <typename T>
struct DecayArrayInternal<T[]>
{
    using type = T *;
};

template <typename T, std::size_t N>
struct DecayArrayInternal<T[N]>
{
    using type = T *;
};

template <typename T>
using DecayArray = DecayArrayInternal<T>::type;

/** Is a character type for the purposes of safe reinterpret_cast. */
template<typename T>
concept IsChar = std::same_as<T, char> || std::same_as<T, unsigned char>;

template<typename T>
concept IsConvertibleToChar = std::same_as<T, char8_t> || std::same_as<T, void> || IsChar<T>;

template<typename T>
concept IsDecayOrPointer = std::is_pointer_v<T> || std::is_pointer_v<DecayArray<T>>;

template<typename From, typename To>
concept ValidQualifiers = requires {
    // Does not discard const
    requires !std::is_const_v<From> || std::is_const_v<To>;
    // Don't deal with volatile
    requires !std::is_volatile_v<From> && !std::is_volatile_v<To>;
};

template<typename From, typename To>
concept BaseCase = requires {
    // Cannot cast away const
    requires ValidQualifiers<From, To>;
    // At base case, neither should be pointers
    requires !std::is_pointer_v<From> && !std::is_pointer_v<To>;
    // Finally are the types compatible?
    requires IsConvertibleToChar<std::remove_cv_t<From>>;
    requires IsChar<std::remove_cv_t<To>>;
};

static_assert(BaseCase<char, char>);
static_assert(BaseCase<unsigned char, char>);
static_assert(BaseCase<char8_t, char>);
static_assert(!BaseCase<const char8_t, char>);
static_assert(!BaseCase<const char8_t, unsigned char>);
static_assert(BaseCase<void, unsigned char>);
// Not legal to cast to char8_t
static_assert(!BaseCase<void, char8_t>);
// No pointers
static_assert(!BaseCase<void *, char8_t>);
static_assert(!BaseCase<char *, char *>);

// Required to be written in the old style because recursion in concepts is not
// allowed. Personally I think the committee hates fun.
template<typename From, typename To, typename = void>
struct RecursionHelper : std::false_type
{};

template<typename From, typename To>
struct RecursionHelper<From, To, std::enable_if_t<BaseCase<From, To>>> : std::true_type
{};

template<typename From, typename To>
struct RecursionHelper<
    From,
    To,
    std::enable_if_t<std::is_pointer_v<From> && std::is_pointer_v<To> && ValidQualifiers<From, To>>>
    : RecursionHelper<std::remove_pointer_t<From>, std::remove_pointer_t<To>>
{};

template<typename From, typename To>
concept IsCharCastable = requires {
    // We only decay arrays in From for safety reasons. There is almost no reason
    // to cast *into* an array and such code probably needs closer inspection
    // anyway.
    requires RecursionHelper<DecayArray<From>, To>::value;
    requires IsDecayOrPointer<From> && std::is_pointer_v<To>;
};

static_assert(!IsCharCastable<char **, char *>);
static_assert(IsCharCastable<char *, char *>);
static_assert(!IsCharCastable<const char *, char *>);
static_assert(!IsCharCastable<volatile char *, char *>);
static_assert(!IsCharCastable<char *, volatile char *>);
static_assert(IsCharCastable<char *, const char *>);
static_assert(IsCharCastable<char **, const char **>);
static_assert(IsCharCastable<char **, const char * const *>);
static_assert(!IsCharCastable<char * const *, const char **>);
static_assert(!IsCharCastable<char, char>);
static_assert(IsCharCastable<const char *, const unsigned char *>);
static_assert(!IsCharCastable<char [64][64], char **>);
static_assert(IsCharCastable<char [64], char *>);
}

/** Casts between character pointers with guaranteed safety. If this compiles,
 * it is at least a sound conversion per C++23 §7.2.1 line 11.
 *
 * This will not let you:
 * - Turn things into void *
 * - Turn things that are not char into char
 * - Turn things into things that are not char
 * - Cast away const
 *
 * At every level in the pointer indirections, \c To must as const or more
 * const than \c From.
 *
 * \c From may be any character pointer or void pointer or an array of characters.
 *
 * N.B. Be careful, the template args are in the possibly-surprising
 * order To, From due to deduction.
 */
template<typename To, typename From>
    requires charptr_cast_detail::IsCharCastable<From, To>
inline To charptr_cast(From p)
{
    // NOLINTNEXTLINE(lix-charptrcast): stop the linter ever getting too clever and causing funny recursion
    return reinterpret_cast<To>(p);
}

}
