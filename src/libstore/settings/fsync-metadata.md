---
name: fsync-metadata
internalName: fsyncMetadata
type: bool
default: true
---
If set to `true`, changes to the Nix store metadata (in
`/nix/var/nix/db`) are synchronously flushed to disk. This improves
robustness in case of system crashes, but reduces performance. The
default is `true`.
