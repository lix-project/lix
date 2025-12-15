---
synopsis: Fix resolving of symlinks in flake paths
issues: [fj#106]
prs: [12286]
cls: [4783]
category: Fixes
credits: [stevalkr, xyenon]
---

Flake paths are now canonicalized to resolve symlinks. This ensures that when a flake is accessed via a symlink, paths are resolved relative to the target directory, not the symlink's location.
