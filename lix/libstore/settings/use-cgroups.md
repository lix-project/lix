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

Be aware that this option is incompatible with various [`nix-build`](@docroot@/command-ref/nix-build.md), [`nix-store`](@docroot@/command-ref/nix-store.md), [`nix store`](@docroot@/command-ref/new-cli/nix3-store.md), and [`nix verify`](@docroot@/command-ref/new-cli/nix3-store.md) sub-commands when using a [`local`](@docroot@/command-ref/new-cli/nix3-help-stores.md#local-store) store which will need the option explicitely turned off with the argument `--no-use-cgroups`.
