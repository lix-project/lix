#include "cli-literate-parser.hh"
#include "lix/libutil/escape-string.hh"
#include "lix/libutil/types.hh"
#include <ranges>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <iostream>
#include <sstream>
#include <variant>

#include "cli-literate-parser.hh"
#include "lix/libutil/escape-string.hh"
#include "lix/libutil/fmt.hh"
#include "lix/libutil/shlex.hh"
#include "lix/libutil/types.hh"
#include "lix/libutil/strings.hh"

static constexpr const bool DEBUG_PARSER = false;

using namespace std::string_literals;
using namespace boost::algorithm;

namespace nix {

namespace cli_literate_parser {

struct Parser
{
    Parser(const std::string input, Config config)
        : input(input)
        , rest(this->input)
        , prompt(config.prompt)
        , indentString(config.indent, ' ')
        , lastWasOutput(false)
        , syntax{}
    {
        assert(!prompt.empty());
    }

    const std::string input;
    std::string_view rest;
    const std::string prompt;
    const std::string indentString;

    /** Last line was output, so we consider a blank to be part of the output */
    bool lastWasOutput;

    /**
     * Nodes of syntax being built.
     */
    std::vector<Node> syntax;

    auto dbg(std::string_view state) -> void
    {
        std::cout << state << ": ";
        escapeString(
            std::cout,
            rest,
            {
                .maxLength = 40,
                .outputAnsiColors = true,
                .escapeNonPrinting = true,
            }
        );
        std::cout << std::endl;
    }

    template<typename T>
    auto pushNode(T node) -> void
    {
        if constexpr (DEBUG_PARSER) {
            std::cout << debugNode(node);
        }
        syntax.emplace_back(node);
    }

    auto parseLiteral(const char c) -> bool
    {
        if (rest.starts_with(c)) {
            rest.remove_prefix(1);
            return true;
        } else {
            return false;
        }
    }

    auto parseLiteral(const std::string_view & literal) -> bool
    {
        if (rest.starts_with(literal)) {
            rest.remove_prefix(literal.length());
            return true;
        } else {
            return false;
        }
    }

    auto parseBool() -> bool
    {
        auto result = false;
        if (parseLiteral("true")) {
            result = true;
        } else if (parseLiteral("false")) {
            result = false;
        } else {
            throw ParseError("true or false", std::string(rest));
        }
        auto untilNewline = parseUntilNewline();
        if (!untilNewline.empty()) {
            throw ParseError("nothing after true or false", untilNewline);
        }
        return result;
    }

    auto parseUntilNewline() -> std::string
    {
        auto pos = rest.find('\n');
        if (pos == std::string_view::npos) {
            throw ParseError("text and then newline", std::string(rest));
        } else {
            // `parseOutput()` sets this to true anyways.
            lastWasOutput = false;
            auto result = std::string(rest, 0, pos);
            rest.remove_prefix(pos + 1);
            return result;
        }
    }

    auto parseIndent() -> bool
    {
        if constexpr (DEBUG_PARSER) {
            dbg("indent");
        }
        if (indentString.empty()) {
            return true;
        }

        if (parseLiteral(indentString)) {
            pushNode(Indent(indentString));
            return true;
        } else {
            if constexpr (DEBUG_PARSER) {
                dbg("indent failed");
            }
            return false;
        }
    }

    auto parseCommand() -> void
    {
        if constexpr (DEBUG_PARSER) {
            dbg("command");
        }
        auto untilNewline = parseUntilNewline();
        pushNode(Command(untilNewline));
    }

    auto parsePrompt() -> void
    {
        if constexpr (DEBUG_PARSER) {
            dbg("prompt");
        }
        if (parseLiteral(prompt)) {
            pushNode(Prompt(prompt));
            if (rest.empty()) {
                return;
            }
            parseCommand();
        } else {
            parseOutput();
        }
    }

    auto parseOutput() -> void
    {
        if constexpr (DEBUG_PARSER) {
            dbg("output");
        }
        auto untilNewline = parseUntilNewline();
        pushNode(Output(untilNewline));
        lastWasOutput = true;
    }

    auto parseAtSign() -> void
    {
        if constexpr (DEBUG_PARSER) {
            dbg("@ symbol");
        }
        if (!parseLiteral('@')) {
            parseOutputOrCommentary();
        }

        if (parseLiteral("args ")) {
            parseArgs();
        } else if (parseLiteral("should-start ")) {
            if constexpr (DEBUG_PARSER) {
                dbg("@should-start");
            }
            auto shouldStart = parseBool();
            pushNode(ShouldStart{shouldStart});
        }
    }

    auto parseArgs() -> void
    {
        if constexpr (DEBUG_PARSER) {
            dbg("@args");
        }
        auto untilNewline = parseUntilNewline();
        pushNode(Args(untilNewline));
    }

    auto parseOutputOrCommentary() -> void
    {
        if constexpr (DEBUG_PARSER) {
            dbg("output/commentary");
        }
        auto oldLastWasOutput = lastWasOutput;
        auto untilNewline = parseUntilNewline();

        auto trimmed = trim_right_copy(untilNewline);

        if (oldLastWasOutput && trimmed.empty()) {
            pushNode(Output{trimmed});
        } else {
            pushNode(Commentary{untilNewline});
        }
    }

    auto parseStartOfLine() -> void
    {
        if constexpr (DEBUG_PARSER) {
            dbg("start of line");
        }
        if (parseIndent()) {
            parsePrompt();
        } else {
            parseAtSign();
        }
    }

    auto parse() && -> ParseResult
    {
        // Begin the recursive descent parser at the start of a new line.
        while (!rest.empty()) {
            parseStartOfLine();
        }
        return std::move(*this).intoParseResult();
    }

    auto intoParseResult() && -> ParseResult
    {
        // Do another pass over the nodes to produce auxiliary results like parsed
        // command line arguments.
        std::vector<std::string> args;
        std::vector<Node> newSyntax;
        auto shouldStart = true;

        for (auto & node : syntax) {
            std::visit(
                overloaded{
                    [&](Args & e) {
                        auto split = shell_split(e.text);
                        args.insert(args.end(), split.begin(), split.end());
                    },
                    [&](ShouldStart & e) { shouldStart = e.shouldStart; },
                    [&](auto & e) {},
                },
                node
            );

            newSyntax.push_back(node);
        }

        return ParseResult{
            .syntax = std::move(newSyntax),
            .args = std::move(args),
            .shouldStart = shouldStart,
        };
    }
};

template<typename View>
auto tidySyntax(View syntax) -> std::vector<Node>
{
    // Note: Setting `lastWasCommand` lets us trim blank lines at the start and
    // end of the output stream.
    auto lastWasCommand = true;
    std::vector<Node> newSyntax;

    for (auto & node : syntax) {
        // Only compare `Command` and `Output` nodes.
        if (std::visit([&](auto && e) { return !e.shouldCompare(); }, node)) {
            continue;
        }

        // Remove blank lines before and after commands. This lets us keep nice
        // whitespace in the test files.
        auto shouldKeep = std::visit(
            overloaded{
                [&](Command & e) {
                    lastWasCommand = true;
                    auto trimmed = trim_right_copy(e.text);
                    if (trimmed.empty()) {
                        return false;
                    } else {
                        e.text = trimmed;
                        return true;
                    }
                },
                [&](Output & e) {
                    std::string trimmed = trim_right_copy(e.text);
                    if (lastWasCommand && trimmed.empty()) {
                        // NB: Keep `lastWasCommand` true in this branch so we
                        // can keep pruning empty output lines.
                        return false;
                    } else {
                        e.text = trimmed;
                        lastWasCommand = false;
                        return true;
                    }
                },
                [&](auto & e) {
                    lastWasCommand = false;
                    return false;
                },
            },
            node
        );

        if (shouldKeep) {
            newSyntax.push_back(node);
        }
    }

    return newSyntax;
}

auto ParseResult::tidyOutputForComparison() -> std::vector<Node>
{
    auto reversed = tidySyntax(std::ranges::reverse_view(syntax));
    auto unreversed = tidySyntax(std::ranges::reverse_view(reversed));
    return unreversed;
}

void ParseResult::interpolatePwd(std::string_view pwd)
{
    std::vector<std::string> newArgs;
    for (auto & arg : args) {
        newArgs.push_back(replaceStrings(arg, "${PWD}", pwd));
    }
    args = std::move(newArgs);
}

const char * ParseError::what() const noexcept
{
    if (what_) {
        return what_->c_str();
    } else {
        auto escaped = escapeString(rest, {.maxLength = 256, .escapeNonPrinting = true});
        auto hint = HintFmt("Parse error: Expected %1%, got:\n%2%", expected, Uncolored(escaped));
        what_ = hint.str();
        return what_->c_str();
    }
}

auto parse(const std::string input, Config config) -> ParseResult
{
    return Parser(input, config).parse();
}

std::ostream & operator<<(std::ostream & output, const Args & node)
{
    return output << "@args " << node.text;
}

std::ostream & operator<<(std::ostream & output, const ShouldStart & node)
{
    return output << "@should-start " << (node.shouldStart ? "true" : "false");
}

std::ostream & operator<<(std::ostream & output, const TextNode & rhs)
{
    return output << rhs.text;
}

void unparseNode(std::ostream & output, const Node & node, bool withNewline)
{
    std::visit(
        [&](const auto & n) { output << n << (withNewline && n.emitNewlineAfter() ? "\n" : ""); },
        node
    );
}

template<typename T>
std::string gtestFormat(T & value)
{
    std::ostringstream formatted;
    unparseNode(formatted, value, true);
    auto str = formatted.str();
    // Needs to be the literal string `\n` and not a newline character to
    // trigger gtest diff printing. Yes seriously.
    boost::algorithm::replace_all(str, "\n", "\\n");
    return str;
}

void PrintTo(const std::vector<Node> & nodes, std::ostream * output)
{
    for (auto & node : nodes) {
        *output << gtestFormat(node);
    }
}

std::string debugNode(const Node & node)
{
    std::ostringstream output;
    output << std::visit([](const auto & n) { return n.kind(); }, node) << ": ";
    std::ostringstream contents;
    unparseNode(contents, node, false);
    escapeString(output, contents.str(), {.escapeNonPrinting = true});
    return output.str();
}

auto ParseResult::debugPrint(std::ostream & output) -> void
{
    ::nix::cli_literate_parser::debugPrint(output, syntax);
}

void debugPrint(std::ostream & output, std::vector<Node> & nodes)
{
    for (auto & node : nodes) {
        output << debugNode(node) << std::endl;
    }
}

} // namespace cli_literate_parser
} // namespace nix
