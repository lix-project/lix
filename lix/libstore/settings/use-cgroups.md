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

Cgroups requires a cgroup delegation according to <https://systemd.io/CGROUP_DELEGATION/>, i.e.
the Nix process (`nix-daemon` or any single user command) should run in a cgroup tree of a parent cgroup which possess the `user.delegate=1` extended attribute.

In this scenario, Nix will run builds in a sibling cgroup named `nix-build-uid-<build user uid>`.
