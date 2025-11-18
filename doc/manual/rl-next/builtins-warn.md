---
synopsis: "Add `builtins.warn` for emitting warnings from Nix code"
cls: [2248]
category: "Features"
credits: [milibopp, Qyriad]
---

Lix now has a builtin function for emitting warnings.
Like `builtins.trace`, it takes two arguments: the message to emit, and the expression to return.
_Unlike_ `builtins.trace`, `builtins.warn` requires the first argument — the message — to be a string.
In the future we may extend `builtins.warn` to accept a more structured API.

To go along with this, we also have a new config setting [`debugger-on-warn`](@docroot@/command-ref/conf-file.md#conf-debugger-on-warn), which, when used with `--debugger`, makes `builtins.warn` also function like [`builtins.break`](@docroot@/language/builtins.md#builtins-break).
