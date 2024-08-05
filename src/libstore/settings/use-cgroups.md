---
name: use-cgroups
internalName: useCgroups
platforms: [linux]
type: bool
default: false
experimentalFeature: cgroups
---
Whether to execute builds inside cgroups.

Cgroups are required and enabled automatically for derivations
that require the `uid-range` system feature.
