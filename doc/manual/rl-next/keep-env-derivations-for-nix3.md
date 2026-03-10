---
synopsis: "`keep-env-derivations` is now supported for nix3 CLI (`nix profile`)"
cls: [5332]
issues: [fj#1095]
category: "Features"
credits: [raito]
---

The `keep-env-derivations` feature is now available for `nix profile`. This allows users to prevent the garbage collection of derivations used to install a profile, even when `keep-derivations = false` (set to `true` by default).

Previously, `nix-env` supported this feature, but `nix profile` **never** did. This caused issues when garbage collection removed the associated `.drv` files, which are required, for example, by vulnerability management tools (e.g. [vulnix](https://github.com/nix-community/vulnix)) for proper operation.

This issue has now been resolved.
