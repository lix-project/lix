---
synopsis: rename 'nix show-config' to 'nix config show'
issues: 7672
prs: 9477
cls: 993
credits: [thufschmitt, ma27]
category: Improvements
---

`nix show-config` was renamed to `nix config show` to be more consistent with the rest of the command-line interface.

Running `nix show-config` will now print a deprecation warning saying to use `nix config show` instead.
