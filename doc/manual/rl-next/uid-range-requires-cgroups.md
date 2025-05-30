---
synopsis: uid-range depends on cgroups
issues: []
cls: [3230]
category: "Breaking Changes"
credits: [raito, horrors]
---

`uid-range` builds now depends on `cgroups`, an experimental feature.

`uid-range` builds already depended upon `auto-allocate-uids`, another experimental feature.

The rationale for doing so is that `uid-range` provides a sandbox with many
UIDs, this is useful for re-mapping them into a nested namespace, e.g. a
container.
