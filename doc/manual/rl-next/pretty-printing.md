---
synopsis: "Eliminate some pretty-printing surprises"
cls: [1616, 1617, 1618]
prs: [11100]
credits: [alois31, roberth]
category: Improvements
---

Some inconsistent and surprising behaviours have been eliminated from the pretty-printing used by the REPL and `nix eval`:
* Lists and attribute sets that contain only a single item without nested structures are no longer sometimes inappropriately indented in the REPL, depending on internal state of the evaluator.
* Empty attribute sets and derivations are no longer shown as `«repeated»`, since they are always cheap to print.
  This matches the existing behaviour of `nix-instantiate` on empty attribute sets.
  Empty lists were never printed as `«repeated»` already.
* The REPL by default does not print nested attribute sets and lists, and indicates elided items with an ellipsis.
  Previously, the ellipsis was printed even when the structure was empty, so that such items do not in fact exist.
  Since this behaviour was confusing, it does not happen any more.

Before:
```
nix-repl> :p let x = 1 + 2; in [ [ x ] [ x ] ]
[
  [
    3
  ]
  [ 3 ]
]

nix-repl> let inherit (import <nixpkgs> { }) hello; in [ hello hello ]
[
  «derivation /nix/store/fqs92lzychkm6p37j7fnj4d65nq9fzla-hello-2.12.1.drv»
  «repeated»
]

nix-repl> let x = {}; in [ x ]
[
  { ... }
]
```

After:
```
nix-repl> :p let x = 1 + 2; in [ [ x ] [ x ] ]
[
  [ 3 ]
  [ 3 ]
]

nix-repl> let inherit (import <nixpkgs> { }) hello; in [ hello hello ]
[
  «derivation /nix/store/fqs92lzychkm6p37j7fnj4d65nq9fzla-hello-2.12.1.drv»
  «derivation /nix/store/fqs92lzychkm6p37j7fnj4d65nq9fzla-hello-2.12.1.drv»
]

nix-repl> let x = {}; in [ x ]
[
  { }
]
```
