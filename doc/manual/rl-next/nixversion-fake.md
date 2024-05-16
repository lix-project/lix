---
synopsis: "`builtins.nixVersion` and `builtins.langVersion` return fixed values"
cls: [558, 1144]
credits: jade
category: Breaking Changes
---

`builtins.nixVersion` now returns a fixed value `"2.18.3-lix"`.

`builtins.langVersion` returns a fixed value `6`, matching CppNix 2.18.

This prevents feature detection assuming that features that exist in Nix
post-Lix-branch-off might exist, even though the Lix version is greater than
the Nix version.

In the future, check for builtins for feature detection. If a feature cannot be
detected by *those* means, please file a Lix bug.
