---
name: abort-on-warn
internalName: abortOnWarn
type: bool
default: false
---
If set to true, [`builtins.warn`](@docroot@/language/builtins.md#builtins-warn) will throw an error when logging a warning.
This will give you a stack trace that leads to the location of the warning.
This is useful for finding information about warnings in third-party Nix code when you can not start the interactive debugger, such as when Nix is called from a non-interactive script. See [`debugger-on-warn`](#conf-debugger-on-warn).
Currently, a stack trace can only be produced when the debugger is enabled, or when evaluation is aborted.
