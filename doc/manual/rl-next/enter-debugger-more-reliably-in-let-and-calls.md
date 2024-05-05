---
synopsis: The `--debugger` will start more reliably in `let` expressions and function calls
prs: 9917
issues: 6649
---

Previously, if you attempted to evaluate this file with the debugger:

```nix
let
  a = builtins.trace "before inner break" (
    builtins.break "hello"
  );
  b = builtins.trace "before outer break" (
    builtins.break a
  );
in
  b
```

Lix would correctly enter the debugger at `builtins.break a`, but if you asked
it to `:continue`, it would skip over the `builtins.break "hello"` expression
entirely.

Now, Lix will correctly enter the debugger at both breakpoints.
