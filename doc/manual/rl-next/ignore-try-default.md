---
synopsis: Repl debugger uses `--ignore-try` by default
issues: [lix#666]
cls: [3488]
category: Breaking Changes
credits: [jade]
---
Previously, using the debugger meant that exceptions thrown in `builtins.tryEval` would trigger the debugger.

However, this caught nixpkgs initialization code, which is unhelpful in the majority of cases, so we changed the default.

To get the old behaviour, use `--no-ignore-try`.

```
$ nix repl --debugger --expr 'with import <nixpkgs> {}; pkgs.hello'
Lix 2.94.0-dev-pre20250625-9a59106
Type :? for help.
error: file 'nixpkgs-overlays' was not found in the Nix search path (add it using $NIX_PATH or -I)

This exception occurred in a 'tryEval' call. Use --ignore-try to skip these.

Added 13 variables.
nix-repl>
```
