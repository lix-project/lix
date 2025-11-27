---
synopsis: "Add an indication of nix-shell nesting depth"
cls: [4657]
issues: [fj#826]
category: "Improvements"
credits: [thubrecht]
---

When in a nix shell (either via a `nix-shell` or a `nix develop` invocation), a variable `NIX_SHELL_LEVEL` is exported to indicate the nesting depth of nix shells.
