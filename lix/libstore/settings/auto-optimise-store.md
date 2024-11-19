---
name: auto-optimise-store
internalName: autoOptimiseStore
type: bool
default: false
---
If set to `true`, Lix automatically detects files in the store
that have identical contents, and replaces them with hard links to
a single copy. This saves disk space. If set to `false` (the
default), you can still run `nix-store --optimise` to get rid of
duplicate files.
