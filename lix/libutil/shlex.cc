#include "lix/libutil/shlex.hh"
#include "lix/libutil/regex.hh"
#include "lix/libutil/strings.hh"

namespace nix {

std::vector<std::string> shell_split(const std::string & input)
{
    std::vector<std::string> result;

    // Hack: `shell_split` is janky and parses ` a` as `{"", "a"}`, so we trim
    // whitespace before starting.
    auto inputTrimmed = trim(input);

    if (inputTrimmed.empty()) {
        return result;
    }

    std::regex whitespace = regex::parse("^\\s+");
    auto begin = inputTrimmed.cbegin();
    std::string currentToken;
    enum State { sBegin, sSingleQuote, sDoubleQuote };
    State state = sBegin;
    auto iterator = begin;

    for (; iterator != inputTrimmed.cend(); ++iterator) {
        if (state == sBegin) {
            std::smatch match;
            if (regex_search(iterator, inputTrimmed.cend(), match, whitespace)) {
                currentToken.append(begin, iterator);
                result.push_back(currentToken);
                iterator = match[0].second;
                if (iterator == inputTrimmed.cend()) {
                    return result;
                }
                begin = iterator;
                currentToken.clear();
            }
        }

        switch (*iterator) {
        case '\'':
            if (state != sDoubleQuote) {
                currentToken.append(begin, iterator);
                begin = iterator + 1;
                state = state == sBegin ? sSingleQuote : sBegin;
            }
            break;

        case '"':
            if (state != sSingleQuote) {
                currentToken.append(begin, iterator);
                begin = iterator + 1;
                state = state == sBegin ? sDoubleQuote : sBegin;
            }
            break;

        case '\\':
            if (state != sSingleQuote) {
                // perl shellwords mostly just treats the next char as part
                // of the string with no special processing
                currentToken.append(begin, iterator);
                begin = ++iterator;
            }
            break;
            // no other relevant cases; silence exhaustiveness compiler warning
            default: break;
        }
    }

    if (state != sBegin) {
        throw ShlexError(input);
    }

    currentToken.append(begin, iterator);
    result.push_back(currentToken);
    return result;
}

}
