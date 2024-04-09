# Benchmarking scripts for Lix

These are very much WIP, and have a few clumsy assumptions that we would
somewhat rather be fixed, but we have committed them to let others be able to
do benchmarking in the mean time.

## Benchmarking procedure

Build some Lixes you want to compare, by whichever means you wish.

Get a computer that is not busy and *strongly preferably* is bare-metal or at
least not a cloud VM (e.g. go make coffee when running benchmarks).

From the root of a Lix checkout, run `./bench/bench.sh resultlink-one
resultlink-two`, where `resultlink-one` and `resultlink-two` are the result
links from the builds you want to test (they can be any directory with bin/nix
in it, however).

To get the summary again, run `./bench/summarize.jq bench/bench-*.json`.

## Example results

(vim tip: `:r !bench/summarize.jq bench/bench-*.json` to dump it directly into
your editor)

```
result-asserts/bin/nix --extra-experimental-features 'nix-command flakes' search --no-eval-cache github:nixos/nixpkgs/e1fa12d4f6
c6fe19ccb59cac54b5b3f25e160870 hello
  mean:     15.993s ± 0.081s
            user: 13.321s | system: 1.865s
  median:   15.994s
  range:    15.829s ... 16.096s
  relative: 1
result/bin/nix --extra-experimental-features 'nix-command flakes' search --no-eval-cache github:nixos/nixpkgs/e1fa12d4f6c6fe19cc
b59cac54b5b3f25e160870 hello
  mean:     15.897s ± 0.075s
            user: 13.248s | system: 1.843s
  median:   15.88s
  range:    15.807s ... 16.047s
  relative: 0.994

---

result/bin/nix --extra-experimental-features 'nix-command flakes' eval -f bench/nixpkgs/pkgs/development/haskell-modules/hackage-packages.nix
  mean:     0.4s ± 0.024s
            user: 0.335s | system: 0.046s
  median:   0.386s
  range:    0.379s ... 0.43s
  relative: 1

result-asserts/bin/nix --extra-experimental-features 'nix-command flakes' eval -f bench/nixpkgs/pkgs/development/haskell-modules/hackage-packages.nix
  mean:     0.404s ± 0.024s
            user: 0.338s | system: 0.046s
  median:   0.386s
  range:    0.384s ... 0.436s
  relative: 1.008

---

result-asserts/bin/nix --extra-experimental-features 'nix-command flakes' eval --raw --impure --expr 'with import <nixpkgs/nixos> {}; system'
  mean:     5.838s ± 0.023s
            user: 5.083s | system: 0.464s
  median:   5.845s
  range:    5.799s ... 5.867s
  relative: 1

result/bin/nix --extra-experimental-features 'nix-command flakes' eval --raw --impure --expr 'with import <nixpkgs/nixos> {}; system'
  mean:     5.788s ± 0.044s
            user: 5.056s | system: 0.439s
  median:   5.79s
  range:    5.715s ... 5.876s
  relative: 0.991

---

GC_INITIAL_HEAP_SIZE=10g result-asserts/bin/nix eval --extra-experimental-features 'nix-command flakes' --raw --impure --expr 'with import <nixpkgs/nixos> {}; system'
  mean:     4.147s ± 0.021s
            user: 3.457s | system: 0.487s
  median:   4.147s
  range:    4.123s ... 4.195s
  relative: 1

GC_INITIAL_HEAP_SIZE=10g result/bin/nix eval --extra-experimental-features 'nix-command flakes' --raw --impure --expr 'with import <nixpkgs/nixos> {}; system'
  mean:     4.149s ± 0.027s
            user: 3.483s | system: 0.456s
  median:   4.142s
  range:    4.126s ... 4.215s
  relative: 1

---
```
