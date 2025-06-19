---
synopsis: "repl-overlays now work in the debugger for flakes"
issues: [fj#777]
cls: [3398]
category: Fixes
credits: [jade]
---
Due to a bug, it was previously not possible to use the debugger on flakes with repl-overlays, or with pure evaluation in general:

```
$ nix repl --pure-eval
Lix 2.94.0-dev-pre20250617-87d99da
Type :? for help.
Loading 'repl-overlays'...
error: access to absolute path '/Users/jade/.config/nix/repl.nix' is forbidden in pure eval mode (use '--impure' to override)
```

This is now fixed.
The contents of the repl-overlays file itself (i.e. most typically the top level lambda in it) will be evaluated in impure mode.
It may be necessary to use `builtins.seq` to force the impure operations to happen first if one wants to do impure operations inside a repl-overlays file in pure evaluation mode.
