---
name: trace
args: [e1, e2]
---
Evaluate *e1* and print its abstract syntax representation on
standard error. Then return *e2*. This function is useful for
debugging.

If the
[`debugger-on-trace`](@docroot@/command-ref/conf-file.md#conf-debugger-on-trace)
option is set to `true` and the `--debugger` flag is given, the
interactive debugger will be started when `trace` is called (like
[`break`](@docroot@/language/builtins.md#builtins-break)).
