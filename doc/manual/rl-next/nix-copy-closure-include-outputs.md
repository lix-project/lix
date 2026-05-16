---
synopsis: "Fix `nix-copy-closure --include-outputs`"
issues: [gh#5105]
cls: [5588]
category: "Fixes"
credits: [rkjnsn]
---

The `--include-outputs` flag for `nix-copy-closure` now works as intended.
Previously, the option was accepted but silently ignored.
