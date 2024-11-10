---
name: system-features
internalName: systemFeatures
type: StringSet
defaultExpr: 'getDefaultSystemFeatures()'
defaultText: '*machine-specific*'
---
A set of system “features” supported by this machine, e.g. `kvm`.
Derivations can express a dependency on such features through the
derivation attribute `requiredSystemFeatures`. For example, the
attribute

    requiredSystemFeatures = [ "kvm" ];

ensures that the derivation can only be built on a machine with the
`kvm` feature.

This setting by default includes `kvm` if `/dev/kvm` is accessible,
`apple-virt` if hardware virtualization is available on macOS,
and the pseudo-features `nixos-test`, `benchmark` and `big-parallel`
that are used in Nixpkgs to route builds to specific machines.
