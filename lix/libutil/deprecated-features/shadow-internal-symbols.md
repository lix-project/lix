---
name: shadow-internal-symbols
internalName: ShadowInternalSymbols
timeline:
  - date: 2024-11-18
    release: 2.92.0
    cls: [2206, 2295]
    message:
      Introduced as a parser error for the symbols `__sub`, `__mul`, `__div`, `__lessThan`.
  - date: 2026-01-30
    release: 2.95.0
    cls: [4788]
    message: Expanded the deprecation to also forbid shadowing the builtin symbols `null`, `true` and `false`.
---
Shadowing symbol names used internally by the parser is deprecated.
As an example, `5 - 3` internally expands to `__sub 5 3` in the parser.
`__sub` is a symbol name in global scope, which could be shadowed by let bindings like any other.
Shadowing internal symbols is deprecated because it can lead to confusing unintended semantics in code.
Using this to override operators with custom implementations is not supported and will lead to unintended semantics.
(For example, overriding `__lessThan` will be ignored within builtin functions that perform comparison operations like sorting.)

Note that the check happens at usage site, so `let __sub = null; in body` remains allowed for as long as `body` does not contain a subtraction operation.
Similarly, `let __sub = null; in __sub` remains allowed here because the explicit `__sub` usage is obvious and there is less potential for confusion.

Affected symbol names:

- `__sub`
- `__mul`
- `__div`
- `__lessThan`
- `true`
- `false`
- `null`
