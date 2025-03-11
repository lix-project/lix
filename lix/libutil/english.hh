#pragma once
///@file

#include <iostream>
#include <algorithm>
#include <iterator>
#include <ranges>
#include <sstream>

namespace nix {

/**
 * Pluralize a given value.
 *
 * If `count == 1`, prints `1 {single}` to `output`, otherwise prints `{count} {plural}`.
 */
std::ostream & pluralize(
    std::ostream & output,
    unsigned int count,
    const std::string_view single,
    const std::string_view plural);


/** Concatenates a given iterator of strings with commas and 'and',
 * while transforming them with the given function.
 * e.g. ["foo", "bar", "baz"] might get concatenated to "foobar, barbar and bazbar".
 * the lambda for this might look like this:
 * `[](std::string & arg) { return arg + "bar"; };`
 */
template<typename F, std::ranges::input_range R>
std::string concatStringsCommaAnd(F transform, const R & args)
{
    std::stringstream result;
    if (args.size() != 0) {
        result << transform(*args.begin());
    }
    if (args.size() >= 2) {
        // will not do anything for size <= 2
        std::for_each(std::next(args.begin()), std::prev(args.end()), [&](auto const & arg) {
            result << ", " << transform(arg);
        });
        result << " and " << transform(*args.rbegin());
    }
    return result.str();
};

}
