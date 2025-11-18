---
name: warn
args: [msg, e2]
---
Evaluate string *msg* and print it on standard error. Then return *e2*.
This function is useful for warning about unexpected conditions without aborting evaluation.
If the [`debugger-on-warn`](@docroot@/command-ref/conf-file.md#conf-debugger-on-trace) option is set to `true` and the `--debugger` flag is given, the interactive debugger will be started when `warn` is called (like [`break`](@docroot@/language/builtins.md#builtins-break)).
