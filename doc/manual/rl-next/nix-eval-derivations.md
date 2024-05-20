---
synopsis: Print derivation paths in `nix eval`
cls: 446
credits: 9999years
category: Improvements
---

`nix eval` previously printed derivations as attribute sets, so commands that print derivations (e.g. `nix eval nixpkgs#bash`) would infinitely loop and segfault.
It now prints the `.drv` path the derivation generates instead.
