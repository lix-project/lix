---
name: ignore-try
internalName: ignoreExceptionsDuringTry
type: bool
default: true
---
If set to true, ignore exceptions inside 'tryEval' calls when evaluating nix expressions in
debug mode (using the --debugger flag). By default the debugger will pause on all exceptions.
