---
synopsis: Forbid impure path accesses in pure evaluation mode again
category: Fixes
cls: [2708]
credits: [alois31]
---
Lix 2.92.0 mistakenly started allowing the access to ancestors of allowed paths in pure evaluation mode.
This made it possible to bypass the purity restrictions, for example by copying arbitrary files to the store:
```nix
builtins.path {
  path = "/";
  filter = …;
}
```
Restore the previous behaviour of prohibiting such impure accesses.
