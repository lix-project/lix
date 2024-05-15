---
synopsis: "`--debugger` can now access bindings from `let` expressions"
prs: 9918
issues: 8827
category: Fixes
credits: 9999years
---

Breakpoints and errors in the bindings of a `let` expression can now access
those bindings in the debugger. Previously, only the body of `let` expressions
could access those bindings.
