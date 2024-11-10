---
name: flake-registry
internalName: flakeRegistry
type: std::string
default: vendored
experimentalFeature: flakes
---
Path or URI of the global flake registry.

URIs are deprecated. When set to 'vendored', defaults to a vendored
copy of https://channels.nixos.org/flake-registry.json.

When empty, disables the global flake registry.
