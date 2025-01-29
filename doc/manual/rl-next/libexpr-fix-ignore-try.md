---
synopsis: "Fix `--debugger --ignore-try`"
issues: []
cls: [2440]
category: "Fixes"
credits: ["bb010g"]
---

When in debug mode (e.g. from using the `--debugger` flag), enabling [`ignore-try`](@docroot@/command-ref/conf-file.md#conf-ignore-try) once again properly disables debug REPLs within [`builtins.tryEval`](@docroot@/language/builtins.md#builtins-tryEval) calls. Previously, a debug REPL would be started as if `ignore-try` was disabled, but that REPL wouldn't actually be in debug mode, and upon exiting the REPL the evaluating process would segfault.
