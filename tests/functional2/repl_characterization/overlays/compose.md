---
args: ['-f', '{PWD}/repl_overlays_compose.nix', '--repl-overlays', '{PWD}/repl-overlays-compose-1.nix {PWD}/repl-overlays-compose-2.nix']
files: ['repl_overlays_compose.nix', 'repl-overlays-compose-1.nix', 'repl-overlays-compose-2.nix']
---

```output
Lix VERSION
Type :? for help.
Loading installable ''...
Added 1 variables.
Loading 'repl-overlays'...
Added 3 variables.
```
Check that multiple `repl-overlays` can compose together
```nix
var
varUsingFinal
```
```output
"abc"

"final value is: puppy"

```
