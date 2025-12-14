---
name: tokens-no-whitespace
internalName: TokensNoWhitespace
timeline:
  - date: 2026-01-30
    release: 2.95.0
    cls: [4782]
    message: Introduced as parser error.
---
The grammar has several bugs around token boundaries, where two adjacent literals are not always required to have whitespace between them: `0a`, `0.00.0` or `foo"1"2`.
This results in the following unexpected behaviors in the language:

- In lists, adjacent tokens will be parsed as several distinct elements, but outside of lists they will be parsed as a function application.
- Because leading zeroes in floating point literals are not allowed, `00012.3` unexpectedly parses as `12 0.3`.
- Nix does not have hexadecimal number literal notation, but a user naively typing `0x10` will not receive a parser error (because it validly parses as `0 x10` instead).

Because the parser cannot be fixed without introducing breaking changes to the language, all token sequences with known confusing semantics are deprecated with a parse error.
To fix this, insert whitespace between tokens to properly separate them.
