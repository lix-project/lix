---
synopsis: Fix nested flake input `follows`
prs: 6621
cls: 994
---

Previously nested-input overrides were ignored; that is, the following did not
override anything, in spite of the `nix3-flake` manual documenting it working:

```
{
  inputs = {
    foo.url = "github:bar/foo";
    foo.inputs.bar.inputs.nixpkgs = "nixpkgs";
  };
}
```

This is useful to avoid the 1000 instances of nixpkgs problem without having
each flake in the dependency tree to expose all of its transitive dependencies
for modification.
