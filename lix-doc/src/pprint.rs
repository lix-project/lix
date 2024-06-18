// SPDX-FileCopyrightText: 2024 Jade Lovelace
// SPDX-FileCopyrightText: 2024 Lunaphied
// SPDX-License-Identifier: BSD-2-Clause OR MIT

use rnix::ast::{Expr, Lambda};
use rowan::ast::AstNode;

/// Pretty-prints the arguments to a function
pub fn pprint_args(lambda: &Lambda) -> String {
    // TODO: handle docs directly on NODE_IDENT args (uncommon case)
    let mut lambda = lambda.clone();
    let mut depth = 0;
    let mut out = String::new();
    loop {
        let arg = lambda.param().unwrap();
        for child in arg.syntax().children_with_tokens() {
            //dbg!(child.kind());
            match child {
                rowan::NodeOrToken::Node(node) => {
                    out.push_str(&node.text().to_string());
                    if node.kind() == rnix::SyntaxKind::NODE_PAT_ENTRY {
                        out.push_str(&",\n");
                    }
                }
                rowan::NodeOrToken::Token(token) => {
                    use rnix::SyntaxKind::{
                        TOKEN_COMMENT, TOKEN_ELLIPSIS, TOKEN_L_BRACE, TOKEN_QUESTION, TOKEN_R_BRACE,
                    };
                    match token.kind() {
                        TOKEN_COMMENT | TOKEN_ELLIPSIS | TOKEN_QUESTION | TOKEN_L_BRACE
                        | TOKEN_R_BRACE => {
                            //dbg!(&token);
                            out.push_str(&token.text().to_string());
                            if token.kind() == TOKEN_COMMENT {
                                out.push('\n');
                            }
                        }
                        _ => {}
                    }
                    //out.push_str(&token.text().to_string());
                }
            }
        }
        out.push_str(": ");
        let body = lambda.body().unwrap();
        if let Expr::Lambda(inner) = body {
            lambda = inner;
            // If we recurse we want the next line of recursion to be indented and on a new line.
            out.push('\n');
            for _ in 0..=depth {
                out.push('\t');
            }
            depth += 1;
        } else {
            // If we don't find an inner lambda we're done with argument handling.
            break;
        }
    }
    out.push_str("...");
    out

    //pprint_arg(lambda.arg());
}
