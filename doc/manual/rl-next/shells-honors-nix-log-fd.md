---
synopsis: "Shells supports $NIX_LOG_FD now"
cls: [4694, 4695]
issues: [fj#336]
category: "Improvements"
credits: [raito]
---

Lix's "debugging" shells (`nix3-develop` and `nix-shell`) now supports
`$NIX_LOG_FD` environment variable.

This means that [hook logging in
stdenv](https://github.com/NixOS/nixpkgs/pull/310387) appears while debugging
derivations via `nix3-develop` or `nix-shell`.
