#include "lix/libutil/strings.hh"
#include "lix/libutil/references.hh"
#include <boost/lexical_cast.hpp>
#include <stdint.h>

namespace nix {

std::vector<char *> stringsToCharPtrs(const Strings & ss)
{
    std::vector<char *> res;
    // This is const cast since this exists for OS APIs that want char *
    for (auto & s : ss) res.push_back(const_cast<char *>(s.data()));
    res.push_back(0);
    return res;
}


template<class C> C tokenizeString(std::string_view s, std::string_view separators)
{
    C result;
    auto pos = s.find_first_not_of(separators, 0);
    while (pos != std::string_view::npos) {
        auto end = s.find_first_of(separators, pos + 1);
        if (end == std::string_view::npos) end = s.size();
        result.insert(result.end(), std::string(s, pos, end - pos));
        pos = s.find_first_not_of(separators, end);
    }
    return result;
}

template Strings tokenizeString(std::string_view s, std::string_view separators);
template StringSet tokenizeString(std::string_view s, std::string_view separators);
template std::vector<std::string> tokenizeString(std::string_view s, std::string_view separators);


std::string chomp(std::string_view s)
{
    size_t i = s.find_last_not_of(" \n\r\t");
    return i == std::string_view::npos ? "" : std::string(s, 0, i + 1);
}


std::string trim(std::string_view s, std::string_view whitespace)
{
    auto i = s.find_first_not_of(whitespace);
    if (i == s.npos) return "";
    auto j = s.find_last_not_of(whitespace);
    return std::string(s, i, j == s.npos ? j : j - i + 1);
}


std::string replaceStrings(
    std::string res,
    std::string_view from,
    std::string_view to)
{
    if (from.empty()) return res;
    size_t pos = 0;
    while ((pos = res.find(from, pos)) != std::string::npos) {
        res.replace(pos, from.size(), to);
        pos += to.size();
    }
    return res;
}


Rewriter::Rewriter(std::map<std::string, std::string> rewrites)
    : rewrites(std::move(rewrites))
{
}

std::string Rewriter::operator()(std::string s)
{
    StringSource src{s};
    RewritingSource inner{RewritingSource::may_change_size, rewrites, src};
    return inner.drain();
}

template<class N>
std::optional<N> string2Int(const std::string_view s)
{
    if (s.substr(0, 1) == "-" && !std::numeric_limits<N>::is_signed)
        return std::nullopt;
    try {
        return boost::lexical_cast<N>(s.data(), s.size());
    } catch (const boost::bad_lexical_cast &) { // NOLINT(lix-foreign-exceptions)
        return std::nullopt;
    }
}

// Explicitly instantiated in one place for faster compilation
template std::optional<unsigned char>  string2Int<unsigned char>(const std::string_view s);
template std::optional<unsigned short> string2Int<unsigned short>(const std::string_view s);
template std::optional<unsigned int> string2Int<unsigned int>(const std::string_view s);
template std::optional<unsigned long> string2Int<unsigned long>(const std::string_view s);
template std::optional<unsigned long long> string2Int<unsigned long long>(const std::string_view s);
template std::optional<signed char> string2Int<signed char>(const std::string_view s);
template std::optional<signed short> string2Int<signed short>(const std::string_view s);
template std::optional<signed int> string2Int<signed int>(const std::string_view s);
template std::optional<signed long> string2Int<signed long>(const std::string_view s);
template std::optional<signed long long> string2Int<signed long long>(const std::string_view s);

template<class N>
std::optional<N> string2Float(const std::string_view s)
{
    try {
        return boost::lexical_cast<N>(s.data(), s.size());
    } catch (const boost::bad_lexical_cast &) { // NOLINT(lix-foreign-exceptions)
        return std::nullopt;
    }
}

template std::optional<double> string2Float<double>(const std::string_view s);
template std::optional<float> string2Float<float>(const std::string_view s);


std::string toLower(const std::string & s)
{
    std::string r(s);
    for (auto & c : r)
        c = std::tolower(c);
    return r;
}


std::string shellEscape(const std::string_view s)
{
    std::string r;
    r.reserve(s.size() + 2);
    r += "'";
    for (auto & i : s) {
        if (i == '\'') {
            // End the single quote, add a single backslash-escaped single quote,
            // then start a single quote again.
            // i.e., `I didn't know` becomes `'I didn'\''t know'`.
            r += "'\\''";
        } else {
            r += i;
        }
    }

    r += '\'';
    return r;
}

std::string bashEscape(const std::string_view s)
{
    std::string r;
    r.reserve(s.size() + 2);
    r += "'";
    for (auto & i : s) {
        if (!std::isprint(i)) {
            // Close the single quote, start an "ANSI-C Quote" ($'foo'), add `\xXX`,
            // close the ANSI-C Quote, and finally start a normal single quote again.
            r += fmt("'$'\\x%02x''", static_cast<unsigned int>(static_cast<unsigned char>(i)));
        } else if (i == '\'') {
            // End the single quote, add a single backslash-escaped single quote,
            // then start a single quote again.
            // i.e., `I didn't know` becomes `'I didn'\''t know'`.
            r += "'\\''";
        } else {
            r += i;
        }
    }

    r += '\'';
    return r;
}

constexpr char base64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(std::string_view s)
{
    std::string res;
    res.reserve((s.size() + 2) / 3 * 4);
    int data = 0, nbits = 0;

    for (char c : s) {
        data = data << 8 | (unsigned char) c;
        nbits += 8;
        while (nbits >= 6) {
            nbits -= 6;
            res.push_back(base64Chars[data >> nbits & 0x3f]);
        }
    }

    if (nbits) res.push_back(base64Chars[data << (6 - nbits) & 0x3f]);
    while (res.size() % 4) res.push_back('=');

    return res;
}


std::string base64Decode(std::string_view s)
{
    constexpr char npos = -1;
    constexpr std::array<char, 256> base64DecodeChars = [&]() {
        std::array<char, 256>  result{};
        for (auto& c : result)
            c = npos;
        for (int i = 0; i < 64; i++)
            result[base64Chars[i]] = i;
        return result;
    }();

    std::string res;
    // Some sequences are missing the padding consisting of up to two '='.
    //                    vvv
    res.reserve((s.size() + 2) / 4 * 3);
    unsigned int d = 0, bits = 0;

    for (char c : s) {
        if (c == '=') break;
        if (c == '\n') continue;

        char digit = base64DecodeChars[(unsigned char) c];
        if (digit == npos)
            throw Error("invalid character in Base64 string: '%c'", c);

        bits += 6;
        d = d << 6 | digit;
        if (bits >= 8) {
            res.push_back(d >> (bits - 8) & 0xff);
            bits -= 8;
        }
    }

    return res;
}


std::string stripIndentation(std::string_view s)
{
    size_t minIndent = 10000;
    size_t curIndent = 0;
    bool atStartOfLine = true;

    for (auto & c : s) {
        if (atStartOfLine && c == ' ')
            curIndent++;
        else if (c == '\n') {
            if (atStartOfLine)
                minIndent = std::max(minIndent, curIndent);
            curIndent = 0;
            atStartOfLine = true;
        } else {
            if (atStartOfLine) {
                minIndent = std::min(minIndent, curIndent);
                atStartOfLine = false;
            }
        }
    }

    std::string res;

    size_t pos = 0;
    while (pos < s.size()) {
        auto eol = s.find('\n', pos);
        if (eol == s.npos) eol = s.size();
        if (eol - pos > minIndent)
            res.append(s.substr(pos + minIndent, eol - pos - minIndent));
        res.push_back('\n');
        pos = eol + 1;
    }

    return res;
}


std::pair<std::string_view, std::string_view> getLine(std::string_view s)
{
    auto newline = s.find('\n');

    if (newline == s.npos) {
        return {s, ""};
    } else {
        auto line = s.substr(0, newline);
        if (!line.empty() && line[line.size() - 1] == '\r')
            line = line.substr(0, line.size() - 1);
        return {line, s.substr(newline + 1)};
    }
}

std::string showBytes(uint64_t bytes)
{
    return fmt("%.2f MiB", bytes / (1024.0 * 1024.0));
}

}
