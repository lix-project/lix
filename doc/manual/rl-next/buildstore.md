---
synopsis: "building directly into caches is no longer allowed"
cls: [6899]
category: Breaking Changes
credits: [horrors]
---

previously it was possible to build directly into a cache by using commands like
```
nix build -j0 --store $cache --builders daemon $things
```
or substitute directly into a cache with commands like
```
nix build --store $cache $things
```

neither of these uses are supported any more. substitution is easily replaced by `nix copy`, builds must now be done to a local or daemon store and copied to the target store using `nix copy` or similar.
