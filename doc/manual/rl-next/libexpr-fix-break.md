---
synopsis: "builtins.break doesn't break expression anymore"
issues: [1165]
cls: [5422]
category: "Fixes"
credits: [blokyk]
---

Wrapping an expression in `builtins.break` used to break some builtins like
`map` and the `is*` functions, which could modify the execution path of code
inadvertently, made debugging nix harder than it already is, and in some cases
even crashed the interpreter. Now, using `break` should be completely
transparent to whatever function receives it as an input, preventing the
above-mentioned issues.
