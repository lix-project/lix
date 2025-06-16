---
synopsis: New cgroup delegation model
issues: [fj#537, fj#77]
cls: [3230]
category: "Breaking Changes"
credits: [raito, horrors, lheckemann]
---

Builds using cgroups (i.e. `use-cgroups = true` and the experimental feature
`cgroups`) now always delegate a cgroup tree to the sandbox.

Compared to the original C++ Nix project, our delegation includes the
`subtree_control` file as well, which means that the sandbox can disable
certain controllers in its own cgroup tree.

This is a breaking change because this requires the Nix daemon to run with an
already delegated cgroup tree by the service manager.

## How to setup the cgroup tree with systemd?

systemd offers knobs to perform the required setup using:

```
[Service]
Delegate=yes
DelegateSubtree=supervisor
```

These directives are now included in our systemd packaging.

## What about using Nix as root without connecting to the daemon?

Builds run as `root` without connecting to the daemon relying on the cgroup
feature are now broken, i.e.

```console
# nix-build --use-cgroups --sandbox ... # will not work
```

Consider doing instead:

```console
# systemd-run --same-dir --wait -p Delegate=yes -p DelegateSubgroup=supervisor nix-build --use-cgroups ...
```

If you need to disable cgroups temporarily, remember that you can do
`NIX_CONF='include /etc/nix/nix.conf\nuse-cgroups = false' nix-build ...` or
`nix-build --no-use-cgroups ...`.

## What about other service managers than systemd?

systemd has a [documentation](https://systemd.io/CGROUP_DELEGATION/) on how to
handle cgroup delegation from service management perspective.

If your service manager adheres to systemd semantics, e.g. writing an extended
attribute `user.delegate=1` on the delegated cgroup tree directory and moving
the `nix-daemon` process inside a cgroup tree to respect the inner process
rule, then, the feature will work as well.

## Why is the cgroup feature still experimental?

While the cgroup feature unlocks many use cases, its behavior and integration (e.g. user experience), especially at scale on build farms or in multi-tenant environments, are not yet fully matured. There’s also potential for deeper systemd integration (e.g. using slices and scopes) that has not been fully explored.

To avoid locking in an unstable interface, we’re keeping the experimental flag until we have validated the feature across a broader range of scenarios, including but not limited to:

* Nix as root
* Hydra-style build farms
* Forgejo CI runners
* Shared remote builders
