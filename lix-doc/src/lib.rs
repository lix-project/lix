// SPDX-FileCopyrightText: 2024 Jade Lovelace
//
// SPDX-License-Identifier: BSD-2-Clause OR MIT

//! library components of nix-doc
pub mod pprint;

use crate::pprint::pprint_args;

use rnix::types::{Lambda, TypedNode};
use rnix::SyntaxKind::*;
use rnix::{NodeOrToken, SyntaxNode, TextUnit, WalkEvent};

use std::ffi::{CStr, CString};
use std::fs;
use std::iter;
use std::os::raw::c_char;
use std::panic;

use std::ptr;

use std::{fmt::Display, str};

pub type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;

const DOC_INDENT: usize = 3;

struct SearchResult {
    /// Name of the function
    identifier: String,

    /// Dedented documentation comments
    doc: String,

    /// Parameter block for the function
    param_block: String,
}

fn find_pos(file: &str, line: usize, col: usize) -> usize {
    let mut lines = 1;
    let mut line_start = 0;
    let mut it = file.chars().enumerate().peekable();
    while let Some((count, ch)) = it.next() {
        if ch == '\n' || ch == '\r' {
            lines += 1;
            let addend = if ch == '\r' && it.peek().map(|x| x.1) == Some('\n') {
                it.next();
                1
            } else {
                0
            };
            line_start = count + addend;
        }

        let col_diff = ((count as i32) - (line_start as i32)).abs() as usize;
        if lines == line && col_diff == col {
            return count;
        }
    }
    unreachable!();
}

impl SearchResult {
    fn format<P: Display>(&self, filename: P, line: usize) -> String {
        format!(
            "**Synopsis:** `{}` = {}\n\n{}\n\n# {}",
            self.identifier.as_str(),
            self.param_block,
            indented(&self.doc, DOC_INDENT),
            format!("{}:{}", filename, line).as_str(),
        )
    }
}

/// Emits a string `s` indented by `indent` spaces
fn indented(s: &str, indent: usize) -> String {
    let indent_s = iter::repeat(' ').take(indent).collect::<String>();
    s.split('\n')
        .map(|line| indent_s.clone() + line)
        .collect::<Vec<_>>()
        .join("\n")
}

/// Cleans up a single line, erasing prefix single line comments but preserving indentation
fn cleanup_single_line<'a>(s: &'a str) -> &'a str {
    let mut cmt_new_start = 0;
    let mut iter = s.char_indices().peekable();
    while let Some((idx, ch)) = iter.next() {
        // peek at the next character, with an explicit '\n' as "next character" at end of line
        let (_, next_ch) = iter.peek().unwrap_or(&(0, '\n'));

        // if we find a character, save the byte position after it as our new string start
        if ch == '#' || (ch == '*' && next_ch.is_whitespace()) {
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

/// Erases indents in comments. This is *almost* a normal dedent function, but it starts by looking
/// at the second line if it can.
fn dedent_comment(s: &str) -> String {
    let mut whitespaces = 0;
    let mut lines = s.lines();
    let first = lines.next();

    // scan for whitespace
    for line in lines.chain(first) {
        let line_whitespace = line.chars().take_while(|ch| ch.is_whitespace()).count();

        if line_whitespace != line.len() {
            // a non-whitespace line, perfect for taking whitespace off of
            whitespaces = line_whitespace;
            break;
        }
    }

    // maybe the first considered line we found was indented further, so let's look for more lines
    // that might have a shorter indent. In the case of one line, do nothing.
    for line in s.lines().skip(1) {
        let line_whitespace = line.chars().take_while(|ch| ch.is_whitespace()).count();

        if line_whitespace != line.len() {
            whitespaces = line_whitespace.min(whitespaces);
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

/// Deletes whitespace and leading comment characters
///
/// Oversight we are choosing to ignore: if you put # characters at the beginning of lines in a
/// multiline comment, they will be deleted.
fn cleanup_comments<S: AsRef<str>, I: DoubleEndedIterator<Item = S>>(comment: &mut I) -> String {
    dedent_comment(
        &comment
            .rev()
            .map(|small_comment| {
                small_comment
                    .as_ref()
                    // space before multiline start
                    .trim_start()
                    // multiline starts
                    .trim_start_matches("/*")
                    // trailing so we can grab multiline end
                    .trim_end()
                    // multiline ends
                    .trim_end_matches("*/")
                    // extra space that was in the multiline
                    .trim()
                    .split('\n')
                    // erase single line comments and such
                    .map(cleanup_single_line)
                    .collect::<Vec<_>>()
                    .join("\n")
            })
            .collect::<Vec<_>>()
            .join("\n"),
    )
}

/// Get the docs for a specific function
pub fn get_function_docs(filename: &str, line: usize, col: usize) -> Option<String> {
    let content = fs::read(filename).ok()?;
    let decoded = str::from_utf8(&content).ok()?;
    let pos = find_pos(&decoded, line, col);
    let rowan_pos = TextUnit::from_usize(pos);
    let tree = rnix::parse(decoded);

    let mut lambda = None;
    for node in tree.node().preorder() {
        match node {
            WalkEvent::Enter(n) => {
                if n.text_range().start() >= rowan_pos && n.kind() == NODE_LAMBDA {
                    lambda = Lambda::cast(n);
                    break;
                }
            }
            WalkEvent::Leave(_) => (),
        }
    }
    let lambda = lambda?;
    let res = visit_lambda("func".to_string(), &lambda);
    Some(res.format(filename, line))
}

fn visit_lambda(name: String, lambda: &Lambda) -> SearchResult {
    // grab the arguments
    let param_block = pprint_args(&lambda);

    // find the doc comment
    let comment = find_comment(lambda.node().clone()).unwrap_or_else(|| "".to_string());

    SearchResult {
        identifier: name,
        doc: comment,
        param_block,
    }
}

fn find_comment(node: SyntaxNode) -> Option<String> {
    let mut node = NodeOrToken::Node(node);
    let mut comments = Vec::new();
    loop {
        loop {
            if let Some(new) = node.prev_sibling_or_token() {
                node = new;
                break;
            } else {
                node = NodeOrToken::Node(node.parent()?);
            }
        }

        match node.kind() {
            TOKEN_COMMENT => match &node {
                NodeOrToken::Token(token) => comments.push(token.text().clone()),
                NodeOrToken::Node(_) => unreachable!(),
            },
            // This stuff is found as part of `the-fn = f: ...`
            // here:                           ^^^^^^^^
            NODE_KEY | TOKEN_ASSIGN => (),
            t if t.is_trivia() => (),
            _ => break,
        }
    }
    let doc = cleanup_comments(&mut comments.iter().map(|c| c.as_str()));
    Some(doc).filter(|it| !it.is_empty())
}

/// Get the docs for a function in the given file path at the given file position and return it as
/// a C string pointer
#[no_mangle]
pub extern "C" fn nd_get_function_docs(
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

/// Call this to free a string from nd_get_function_docs
#[no_mangle]
pub extern "C" fn nd_free_string(s: *const c_char) {
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
    fn test_bytepos() {
        let fakefile = "abc\ndef\nghi";
        assert_eq!(find_pos(fakefile, 2, 2), 5);
    }

    #[test]
    fn test_bytepos_cursed() {
        let fakefile = "abc\rdef\r\nghi";
        assert_eq!(find_pos(fakefile, 2, 2), 5);
        assert_eq!(find_pos(fakefile, 3, 2), 10);
    }

    #[test]
    fn test_comment_stripping() {
        let ex1 = ["/* blah blah blah\n      foooo baaar\n   blah */"];
        assert_eq!(
            cleanup_comments(&mut ex1.iter()),
            "blah blah blah\n   foooo baaar\nblah"
        );

        let ex2 = ["# a1", "#    a2", "# aa"];
        assert_eq!(cleanup_comments(&mut ex2.iter()), "aa\n   a2\na1");
    }

    #[test]
    fn test_dedent() {
        let ex1 = "a\n   b\n   c\n     d";
        assert_eq!(dedent_comment(ex1), "a\nb\nc\n  d");
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
}
