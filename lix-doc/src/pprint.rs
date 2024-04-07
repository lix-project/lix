// SPDX-FileCopyrightText: 2024 Jade Lovelace
//
// SPDX-License-Identifier: BSD-2-Clause OR MIT

use rnix::types::{Lambda, TypedNode};
use rnix::SyntaxKind::*;

/// Pretty-prints the arguments to a function
pub fn pprint_args(lambda: &Lambda) -> String {
    // TODO: handle docs directly on NODE_IDENT args (uncommon case)
    let mut lambda = lambda.clone();
    let mut out = String::new();
    loop {
        let arg = lambda.arg().unwrap();
        match arg.kind() {
            NODE_IDENT => {
                out += &format!("*{}*", &arg.to_string());
                out.push_str(": ");
                let body = lambda.body().unwrap();
                if body.kind() == NODE_LAMBDA {
                    lambda = Lambda::cast(body).unwrap();
                } else {
                    break;
                }
            }
            NODE_PATTERN => {
                out += &format!("*{}*", &arg.to_string());
                out.push_str(": ");
                break;
            }
            t => {
                unreachable!("unhandled arg type {:?}", t);
            }
        }
    }
    out.push_str("...");
    out

    //pprint_arg(lambda.arg());
}
