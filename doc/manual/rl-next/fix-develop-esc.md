---
synopsis: Fix develop shells for derivations with escape codes
issues: [fj#991]
cls: [4154, 4155]
category: Fixes
credits: [Qyriad]
---

ASCII control characters (including `\e`, used for ANSI escape codes) in derivation variables are now correctly escaped for `nix develop` and `nix print-dev-env`, instead of erroring.
