---
synopsis: Enter the `--debugger` when `builtins.trace` is called if `debugger-on-trace` is set
prs: 9914
category: Features
credits: 9999years
---

If the `debugger-on-trace` option is set and `--debugger` is given,
`builtins.trace` calls will behave similarly to `builtins.break` and will enter
the debug REPL. This is useful for determining where warnings are being emitted
from.
