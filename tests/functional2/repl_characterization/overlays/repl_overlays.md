---
args: ['-f', '{PWD}/repl_overlays.nix', '--repl-overlays', '{PWD}/repl-overlay-packages-is-pkgs.nix']
files: ['repl_overlays.nix', 'repl-overlay-packages-is-pkgs.nix']
---

```output
Lix VERSION
Type :? for help.
Loading installable ''...
Added 1 variables.
Loading 'repl-overlays'...
Added 2 variables.
```

Check basic `repl-overlays` functionality.

```nix
pkgs
```
```output
{ default = "my package"; }

```
