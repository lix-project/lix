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
