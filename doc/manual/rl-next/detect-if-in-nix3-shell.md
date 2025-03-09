---
synopsis: Add a straightforward way to detect if in a Nix3 Shell
issues: [nix#6677, nix#3862]
cls: [2090]
category: Fixes
credits: [9p4]
---

Running `nix shell` or `nix develop` will now set `IN_NIX_SHELL` to
either `pure` or `impure`, depending on whether `--ignore-environment`
is passed. `nix develop` will always be an impure environment.
