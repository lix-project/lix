---
args: ['--repl-overlays', '{PWD}/repl-overlay-no-dotdotdot.nix']
should_fail: True
files: ['repl-overlay-no-dotdotdot.nix']
---

`repl-overlays` that try to parse the `info` argument without a `...` error.

```output
Lix VERSION
Type :? for help.
Loading 'repl-overlays'...
error: Expected first argument of repl-overlays to have ... to allow future versions of Lix to add additional attributes to the argument
       at /pwd/repl-overlay-no-dotdotdot.nix:4:3:
            3| in
            4|   {currentSystem}: final: prev: {
             |   ^
            5|     inherit puppy;
```
