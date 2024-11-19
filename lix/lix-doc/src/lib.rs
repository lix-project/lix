// SPDX-FileCopyrightText: 2024 Jade Lovelace
// SPDX-FileCopyrightText: 2024 Lunaphied
// SPDX-License-Identifier: BSD-2-Clause OR MIT

//! library components of nix-doc
pub mod pprint;

use crate::pprint::pprint_args;

use rnix::ast::{self, Lambda};
use rnix::{NodeOrToken, SyntaxKind};
use rnix::SyntaxNode;


// Needed because rnix fucked up and didn't reexport this, oops.
use rowan::ast::AstNode;

use std::ffi::{CStr, CString};
use std::fs;
use std::os::raw::c_char;
use std::panic;

use std::ptr;

use std::{fmt::Display, str};

pub type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;

struct SearchResult {
    /// Name of the function
    identifier: String,

    /// Dedented documentation comment
    doc: String,

    /// Parameter block for the function
    param_block: String,
}

impl SearchResult {
    fn format<P: Display>(&self, filename: P, line: usize) -> String {
        format!(
            "**Synopsis:** `{}` = {}\n\n{}\n\n# {}",
            self.identifier.as_str(),
            self.param_block,
            self.doc,
            format!("{}:{}", filename, line).as_str(),
        )
    }
}

/// Converts Nix compatible line endings (Nix accepts `\r`, `\n`, *and* `\r\n` as endings), to
/// standard `\n` endings for use within Rust land.
fn convert_endings(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    let mut it = s.chars().peekable();

    while let Some(ch) = it.next() {
        if ch == '\n' || ch == '\r' {
            out.push('\n');
            if ch == '\r' && it.peek().map(|&c| c == '\n').unwrap_or(false) {
                // Consume `\n` in `\r\n`.
                it.next();
            }
        } else {
            out.push(ch);
        }
    }

    out
}

/// Converts the position information from Lix itself into an character index into the file itself.
/// Expects an input string that's already had it's line endings normalized.
///
/// Note that this returns a *byte* offset, not a character offset.
fn find_pos(s: &str, line: usize, col: usize) -> usize {
    // Nix line positions are 1-indexed.
    let mut lines = 1;
    for (byte_pos, ch) in s.char_indices() {
        // If we find a newline, increase the line count.
        if ch == '\n' {
            lines += 1;
        }

        // We've arrived at the correct line.
        if lines == line {
            // Column position is 1-indexed, and it's a *byte* offset, because Nix doesn't actually
            // support UTF-8. Rust does though, so we need to convert to a proper byte index to
            // match rnix. Lix also doesn't consider the line endings part of the column offset so
            // we implicitly add one to advance to the character *after* that.
            return byte_pos + col;
        }
    }

    // If things never match that should be literally impossible.
    unreachable!();
}

/// Represents a forwarded token from rnix's AST over to lix-doc.
#[derive(Debug, Clone)]
enum DocToken {
    Comment(String),
    Whitespace(String),
}

/// Determine if a given token string contains more than two newlines, this is used to determine when
/// we hit blank lines between comments indicating a contextually unrelated comment.
fn has_empty_line(tok: &DocToken) -> bool {
    // It's either solely whitespace with two newlines inside somewhere, or it's
    // contained inside a comment token and we don't want to count that as empty.
    if let DocToken::Whitespace(s) = tok {
        s.chars().filter(|&c| c == '\n').take(2).count() == 2
    } else {
        false
    }
}

/// Cleans up a single line, erasing prefix single line comments but preserving indentation
// NOTE: We have a bit of a conflict of interest problem here due to the inconsistent format of
// doc comments. Some doc comments will use a series of single line comments that may then contain `*`
// characters to represent a list. Some will be multiline comments that don't prefix individual lines
// with `*`, only using them for lists directly, and some will prefix lines with `*` as a leading
// character to mark the block. There's no way to disambiguate all three, but we do our best to
// make the common case pretty.
fn cleanup_single_line(s: &str) -> &str {
    let mut cmt_new_start = 0;
    let mut iter = s.char_indices().peekable();
    while let Some((idx, ch)) = iter.next() {
        // peek at the next character, with an explicit '\n' as "next character" at end of line
        let (_, next_ch) = iter.peek().unwrap_or(&(0, '\n'));

        // if we find a character, save the byte position after it as our new string start
        // This has special handling for `>` because some Nixpkgs documentation has `*>` right
        // after the start of their doc comments, and we want to strip the `*` still.
        if ch == '#' || (ch == '*' && (*next_ch == '>' || next_ch.is_whitespace())) {
            cmt_new_start = idx + 1;
            break;
        }
        // if, instead, we are on a line with no starting comment characters, leave it alone as it
        // will be handled by dedent later
        if !ch.is_whitespace() {
            break;
        }
    }
    &s[cmt_new_start..]
}

/// Erases indents in comments based on the indentation of the first line.
fn dedent_comment(s: &str) -> String {
    let mut whitespaces = 0;

    // scan for whitespace
    for line in s.lines() {
        let line_whitespace = line.chars().take_while(|ch| ch.is_whitespace()).count();

        if line_whitespace != line.len() {
            // a non-whitespace line, perfect for taking whitespace off of
            whitespaces = line_whitespace;
            break;
        }
    }

    // delete up to `whitespaces` whitespace characters from each line and reconstitute the string
    let mut out = String::new();
    for line in s.lines() {
        let content_begin = line.find(|ch: char| !ch.is_whitespace()).unwrap_or(0);
        out.push_str(&line[content_begin.min(whitespaces)..]);
        out.push('\n');
    }

    out.truncate(out.trim_end_matches('\n').len());
    out
}

/// Takes a series of comment and whitespace strings and output a clean single block of text to use
/// as the output documentation comment block.
///
/// This function expects to be given the tokens in reverse order (proceeding upwards from the
/// first comment above the definitions), this allows us to properly enforce the below conditions.
/// The output from this function will be reordered and ready for display.
///
/// The two types of documentation comments we expect are:
///
/// - A single multiline comment not whitespace separated from the start.
/// - A series of back to back single line comments not separated by whitespace.
///
/// Any other combination will be filtered out.
///
/// Once an empty line is encountered, we know no more valid documentation comments remain and stop.
fn cleanup_comments<I: Iterator<Item = DocToken>>(tokens: &mut I) -> String {
    // Keep track of when we've found a single line and multiline comment, we use this to
    // only process a single multiline or back to back single lines.
    let mut found_single_line = false;

    // Comments that have survived our filtering phase and should be cleaned up.
    let mut valid = vec![];

    // Filter out comments that don't meet the characteristics of documentation comments.
    for tok in tokens {
        if has_empty_line(&tok) {
            // Take tokens until we hit whitespace containing an empty line.
            break;
        }

        // Only care about comments from this point on.
        if let DocToken::Comment(comment) = tok {
            // Now determine if it's a single line comment.
            let is_single_line = comment.starts_with('#');

            // We've found a single line comment if we've found one before or we just found one.
            found_single_line |= is_single_line;

            // What we do next is only special when we hit a multiline comment.
            if !is_single_line {
                // If we've hit a multiline comment as our first comment, take that one alone.
                if !found_single_line {
                    // Otherwise we've hit a multiline comment immediately and this is our
                    // one and only doc comment to worry about.
                    valid.push(comment);
                }
                // Otherwise we've hit a multiline comment after single line comments, in either
                // case this means we're done processing comments.
                break;
            }

            // Otherwise this is a new single line comment to push to the stack.
            valid.push(comment);
        }
    }

    // Cleanup comments for user consumption.
    dedent_comment(
        &valid
            .into_iter()
            .rev()
            .map(|small_comment| {
                small_comment
                    // Trim off start of multiline comments.
                    .trim_start_matches("/*")
                    // Trim off end of multiline comments.
                    .trim_end_matches("*/")
                    // Trim off any internal whitespace that's trapped inside comments themselves.
                    .trim()
                    // Split comments by newlines to extract lines of multiline comments.
                    .split('\n')
                    // Cleanup single line comments and a few more tweaks for multiline comments.
                    .map(cleanup_single_line)
                    .collect::<Vec<_>>()
                    // Reconstruct the multiline comment's whitespace.
                    .join("\n")
            })
            .collect::<Vec<_>>()
            // We've found that when multiple back to back single line comments are used in Nixpkgs,
            // they make more sense to represent as if someone inserted line breaks into the Markdown
            // properly, so we join them with linebreaks that markdown will pass through.
            .join("\n\n"),
    )
}

/// Get the docs for a specific function.
// TODO: Improve error reporting?
pub fn get_function_docs(filename: &str, line: usize, col: usize) -> Option<String> {
    let content = fs::read(filename).ok()?;
    let decoded = convert_endings(str::from_utf8(&content).ok()?);
    let pos = find_pos(&decoded, line, col);
    let rowan_pos = rnix::TextSize::from(pos as u32);

    // The minimum length of a lambda is 4 characters and thus the range we're looking for must be
    // at least 4 characters long `_: 3` being an example of a minimal length lambda.
    let rowan_range = rnix::TextRange::at(rowan_pos, 4.into());

    // Parse the file  using rnix.
    let root = rnix::Root::parse(&decoded).ok().ok()?;

    // Extract the inner expression that represents the Root node and extract the top level expression.
    let expr = root.expr()?;

    // There are two cases we have to be able to handle
    // 1. A straightforward definition with an attrset binding to a lambda that's defined inline.
    // 2. A lambda defined in a standalone file where the attrset binding imports that file directly.
    // The latter case will not be able to find the binding so we must be able to handle not finding it.

    // Find the deepest node or token that covers the position given by Lix.
    let covering = expr.syntax().covering_element(rowan_range);

    // Climb up until we find the lambda node that contains that token.
    let mut lambda = None;
    for ancestor in covering.ancestors() {
        if ancestor.kind() == SyntaxKind::NODE_LAMBDA {
            lambda = Some(ancestor);
            break;
        }
    }

    // There is literally always a lambda or something has gone very very wrong.
    let lambda =
        ast::Lambda::cast(
            lambda.expect("no lambda found; what.")
        ) .expect("not a rnix::ast::Lambda; what.");

    // Search up, hopefully to find the binding so we can get the identifier name.
    // TODO: Just provide this directly from the C++ code to make it possible to always have the correct identifier.
    let mut binding = None;
    for ancestor in lambda.syntax().ancestors() {
        if ancestor.kind() == SyntaxKind::NODE_ATTRPATH_VALUE {
            binding = Some(ancestor);
        }
    }

    // Convert the binding to an identifier if it was found, otherwise use a placeholder.
    let identifier;
    identifier = match binding.clone() {
        Some(binding) => ast::AttrpathValue::cast(binding)
            .expect("not an rnix::ast::AttrpathValue; what")
            .attrpath()
            .expect("AttrpathValue has no attrpath; what.")
            .to_string(),
        _ => "<unknown binding>".to_string(),
    };

    // Find all the comments on the binding or the lambda if we have to fall back.
    let comment_node = binding.as_ref().unwrap_or(lambda.syntax());
    let comment = find_comment(comment_node).unwrap_or_else(String::new);

    // And display them properly for the markdown function in Lix.
    Some(visit_lambda(identifier, comment, &lambda).format(filename, line))
}

fn visit_lambda(name: String, comment: String, lambda: &Lambda) -> SearchResult {
    // grab the arguments
    let param_block = pprint_args(lambda);

    SearchResult {
        identifier: name,
        doc: comment,
        param_block,
    }
}

fn find_comment(node: &SyntaxNode) -> Option<String> {
    let mut it = node
        .siblings_with_tokens(rowan::Direction::Prev)
        // Skip ourselves as we're always the first token returned.
        .skip(1)
        .peekable();

    // Consume up to one whitespace token before the first comment. There might not always be
    // whitespace such as the (rather unusual) case of `/* meow */x = a: 3`.
    if matches!(it.peek(), Some(NodeOrToken::Token(token)) if token.kind() == SyntaxKind::TOKEN_WHITESPACE) {
        it.next();
    }

    let comments = it.map_while(|element| match element {
            NodeOrToken::Token(token) => {
                match token.kind() {
                    // Map the tokens we're interested in to our internal token type.
                    SyntaxKind::TOKEN_COMMENT => Some(DocToken::Comment(token.text().to_owned())),
                    SyntaxKind::TOKEN_WHITESPACE => {
                        Some(DocToken::Whitespace(token.text().to_owned()))
                    }
                    // If we hit a different token type, we know we've gone past relevant comments
                    // and should stop.
                    _ => None,
                }
            }
            // If we hit a node entry we've definitely gone past comments that would be related to
            // this node and we should retreat.
            _ => None,
        });

    // For the curious, `into_iter()` here consumes the binding producing an owned value allowing us to avoid
    // making the original binding mutable, we don't reuse it later so this is a cute way to handle it, though
    // there's probably a better way we just can't remember.
    Some(cleanup_comments(&mut comments.into_iter())).filter(|c| !c.is_empty())
}

/// Get the docs for a function in the given file path at the given file position and return it as
/// a C string pointer
#[no_mangle]
pub extern "C" fn lixdoc_get_function_docs(
    filename: *const c_char,
    line: usize,
    col: usize,
) -> *const c_char {
    let fname = unsafe { CStr::from_ptr(filename) };
    fname
        .to_str()
        .ok()
        .and_then(|f| {
            panic::catch_unwind(|| get_function_docs(f, line, col))
                .map_err(|e| {
                    eprintln!("panic!! {:#?}", e);
                    e
                })
                .ok()
        })
        .flatten()
        .and_then(|s| CString::new(s).ok())
        .map(|s| s.into_raw() as *const c_char)
        .unwrap_or(ptr::null())
}

/// Call this to free a string from `lixdoc_get_function_docs`.
#[no_mangle]
pub extern "C" fn lixdoc_free_string(s: *const c_char) {
    unsafe {
        // cast note: this cast is turning something that was cast to const
        // back to mut
        drop(CString::from_raw(s as *mut c_char));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_line_conversion() {
        let fakefile = "abc\rdef\r\nghi";
        assert_eq!(convert_endings(fakefile), "abc\ndef\nghi");
    }

    #[test]
    fn test_bytepos() {
        let fakefile = "abc\ndef\nghi";
        assert_eq!(find_pos(fakefile, 2, 2), 5);
    }

    #[test]
    fn test_bytepos_unusual() {
        let fakefile = convert_endings("abc\rdef\r\nghi");
        assert_eq!(find_pos(&fakefile, 2, 2), 5);
        assert_eq!(find_pos(&fakefile, 3, 2), 9);
    }

    /// This test is to check that we correctly resolve byte positions even when inconsistent with
    /// character positions.
    #[test]
    fn test_bytepos_cursed() {
        let fakefile = "hello\nwórld";
        // Try to find the position of the `r` after world, which will be wrong if we don't handle
        // UTF-8 properly.
        let pos = find_pos(&fakefile, 2, 4);
        dbg!(&fakefile[pos..]);
        assert_eq!(pos, 9)
    }

    #[test]
    fn test_comment_stripping() {
        let ex1 = [DocToken::Comment(
            "/* blah blah blah\n      foooo baaar\n   blah */".to_string(),
        )];
        assert_eq!(
            cleanup_comments(&mut ex1.into_iter()),
            "blah blah blah\n      foooo baaar\n   blah"
        );

        let ex2 = ["# a1", "#    a2", "# aa"]
            .into_iter()
            .map(|s| DocToken::Comment(s.to_string()));
        assert_eq!(cleanup_comments(&mut ex2.into_iter()), "aa\n\n   a2\n\na1");
    }

    #[test]
    fn test_dedent() {
        let ex1 = "a\n   b\n   c\n     d";
        assert_eq!(dedent_comment(ex1), ex1);
        let ex2 = "a\nb\nc";
        assert_eq!(dedent_comment(ex2), ex2);
        let ex3 = "   a\n   b\n\n     c";
        assert_eq!(dedent_comment(ex3), "a\nb\n\n  c");
    }

    #[test]
    fn test_single_line_comment_stripping() {
        let ex1 = "    * a";
        let ex2 = "    # a";
        let ex3 = "   a";
        let ex4 = "   *";
        assert_eq!(cleanup_single_line(ex1), " a");
        assert_eq!(cleanup_single_line(ex2), " a");
        assert_eq!(cleanup_single_line(ex3), ex3);
        assert_eq!(cleanup_single_line(ex4), "");
    }

    #[test]
    fn test_single_line_retains_bold_headings() {
        let ex1 = "   **Foo**:";
        assert_eq!(cleanup_single_line(ex1), ex1);
    }

    // TODO: Next CL
    //#[test]
    //fn comment_test_complex() {
    //    let testcase = r#"
    //    rec {
    //        /*
    //           Hello
    //           23
    //             This is a comment.
    //             this is another comment.
    //             and this is a third comment.
    //                          Way
    //              go
    //        */
    //        meow = { g }: {a, b ? 4, ...}: g: c: 5;
    //        # And another comment.
    //        cat = 34;
    //        # inner layer.
    //        "inner-layer" = outer: meow;
    //    }
    //    "#;
    //    // Need to find the location of the lambda, we do a quick hack.
    //    let location = dbg!(testcase.find("{ g }").unwrap() as u32);
    //
    //    //get_function_docs(filename, line, col)
    //}
}
