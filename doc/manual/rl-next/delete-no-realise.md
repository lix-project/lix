---
synopsis: "`nix store delete` no longer builds paths"
cls: [2782]
category: Fixes
credits: lheckemann
---

`nix store delete` no longer realises the installables
specified. Previously, `nix store delete nixpkgs#hello` would download
hello only to immediately delete it again. Now, it exits with an error
if given an installable that isn't in the store.
