#pragma once
///@file

#include "lix/libutil/error.hh"
#include <compare>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace nix {
namespace cli_literate_parser {

// ------------------------- NODES -------------------------
//
// To update golden test files while preserving commentary output and other `@`
// directives, we need to keep commentary output around after parsing.

struct BaseNode {
  virtual ~BaseNode() = default;

  virtual auto shouldCompare() const -> bool { return false; }

  virtual auto kind() const -> std::string = 0;
  virtual auto emitNewlineAfter() const -> bool = 0;

  auto operator<=>(const BaseNode &rhs) const = default;
};

/**
 * A node containing text. The text should be identical to how the node was
 * written in the input file.
 */
struct TextNode : BaseNode {
  std::string text;

  explicit TextNode(std::string text) : text(text) {}
};

std::ostream &operator<<(std::ostream &output, const TextNode &node);

#define DECLARE_TEXT_NODE(NAME, NEEDS_NEWLINE, SHOULD_COMPARE)                 \
  struct NAME : TextNode {                                                     \
    using TextNode::TextNode;                                                  \
    ~NAME() override = default;                                                \
                                                                               \
    auto kind() const -> std::string override { return #NAME; }                \
    auto emitNewlineAfter() const -> bool override { return NEEDS_NEWLINE; }   \
    auto shouldCompare() const -> bool override { return SHOULD_COMPARE; }     \
  };

/* name, needsNewline, shouldCompare */
DECLARE_TEXT_NODE(Prompt, false, false)
DECLARE_TEXT_NODE(Command, true, true)
DECLARE_TEXT_NODE(Output, true, true)
DECLARE_TEXT_NODE(Commentary, true, false)
DECLARE_TEXT_NODE(Args, true, false)
DECLARE_TEXT_NODE(Indent, false, false)

#undef DECLARE_TEXT_NODE

struct ShouldStart : BaseNode {
  bool shouldStart;

  ShouldStart(bool shouldStart) : shouldStart(shouldStart) {}
  ~ShouldStart() override = default;
  auto emitNewlineAfter() const -> bool override { return true; }
  auto kind() const -> std::string override { return "should-start"; }

  auto operator<=>(const ShouldStart &rhs) const = default;
};
std::ostream &operator<<(std::ostream &output, const ShouldStart &node);

/**
 * Any syntax node, including those that are cosmetic.
 */
using Node = std::variant<Prompt, Command, Output, Commentary, Args,
                          ShouldStart, Indent>;

/** Unparses a node into the exact text that would have created it, including a
 * newline at the end if present, if withNewline is set */
void unparseNode(std::ostream &output, const Node &node,
                 bool withNewline = true);

std::string debugNode(const Node &node);
void debugPrint(std::ostream &output, std::vector<Node> &nodes);

/**
 * Override gtest printing for lists of nodes.
 */
void PrintTo(std::vector<Node> const &nodes, std::ostream *output);

/**
 * The result of parsing a test file.
 */
struct ParseResult {
  /**
   * A set of nodes that can be used to reproduce the input file. This is used
   * to implement updating the test files.
   */
  std::vector<Node> syntax;

  /**
   * Extra CLI arguments.
   */
  std::vector<std::string> args;

  /**
   * Should the program start successfully?
   */
  bool shouldStart = false;

  /**
   * Replace `$PWD` with the given value in `args`.
   */
  void interpolatePwd(std::string_view pwd);

  /**
   * Tidy `syntax` to remove unnecessary nodes.
   */
  auto tidyOutputForComparison() -> std::vector<Node>;

  auto debugPrint(std::ostream &output) -> void;
};

/**
 * A parse error.
 */
struct ParseError : BaseException {
  std::string expected;
  std::string rest;

  ParseError(std::string expected, std::string rest)
      : expected(expected), rest(rest) {}

  const char *what() const noexcept override;

private:
  /**
   * Cached formatted contents of `what()`.
   */
  mutable std::optional<std::string> what_;
};

struct Config {
  /**
   * The prompt string to look for.
   */
  std::string prompt;
  /**
   * The number of spaces of indent for commands and output.
   */
  size_t indent = 2;
};

/*
 * A recursive descent parser for literate test cases for CLIs.
 *
 * FIXME: implement merging of these, so you can auto update cases that have
 * comments.
 *
 * Syntax:
 * ```
 * ( COMMENTARY
 * | INDENT PROMPT COMMAND
 * | INDENT OUTPUT
 * | @args ARGS
 * | @should-start ( true | false )) *
 * ```
 *
 * e.g.
 * ```
 * commentary commentary commentary
 * @args --foo
 * @should-start false
 *   nix-repl> :t 1
 *   an integer
 * ```
 *
 * Yields something like:
 * ```
 * Commentary "commentary commentary commentary"
 * Args "--foo"
 * ShouldStart false
 * Command ":t 1"
 * Output "an integer"
 * ```
 *
 * Note: one Output line is generated for each line of the sources, because
 * this is effectively necessary to be able to align them in the future to
 * auto-update tests.
 */
auto parse(std::string input, Config config) -> ParseResult;

}; // namespace cli_literate_parser
}; // namespace nix
