---
name: debugger-on-warn
internalName: debuggerOnWarn
type: bool
default: false
---
If set to true and the `--debugger` flag is given, [`builtins.warn`](@docroot@/language/builtins.md#builtins-warn) will enter the debugger like [`builtins.break`](@docroot@/language/builtins.md#builtins-break).
This is useful for debugging warnings in third-party Nix code.
Use [`debugger-on-trace`](#conf-debugger-on-trace) to also enter the debugger on legacy warnings that are logged with [`builtins.trace`](@docroot@/language/builtins.md#builtins-trace).
