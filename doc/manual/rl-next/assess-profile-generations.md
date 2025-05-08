---
synopsis: Assess current profile generations pointers in `nix doctor`
cls: [3108]
category: Improvements
credits: [raito]
---

Added a new check to `nix doctor` that verifies whether the current generation of
a Nix profile can be resolved. This helps users diagnose issues with broken or
misconfigured profile symlinks.

This helps determining if you have broken symlinks or misconfigured packaging.
