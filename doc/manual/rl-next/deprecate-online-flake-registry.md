---
synopsis: "Deprecate the online flake registries and vendor the default registry"
cls: 1127
credits: midnightveil
issues: [fj#183, fj#110, fj#116, 8953, 9087]
category: Breaking Changes
---

The online flake registry [https://channels.nixos.org/flake-registry.json](https://channels.nixos.org/flake-registry.json) is not pinned in any way,
and the targets of the indirections can both update or change entirely at any
point. Furthermore, it is refetched on every use of a flake reference, even if
there is a local flake reference, and even if you are offline (which breaks).

For now, we deprecate the (any) online flake registry, and vendor a copy of the
current online flake registry. This makes it work offline, and ensures that
it won't change in the future.
