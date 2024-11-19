---
name: debugger-on-trace
internalName: builtinsTraceDebugger
type: bool
default: false
---
If set to true and the `--debugger` flag is given,
[`builtins.trace`](@docroot@/language/builtins.md#builtins-trace) will
enter the debugger like
[`builtins.break`](@docroot@/language/builtins.md#builtins-break).

This is useful for debugging warnings in third-party Nix code.
