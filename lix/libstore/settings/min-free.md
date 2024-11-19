---
name: min-free
internalName: minFree
type: uint64_t
default: 0
---
When free disk space in `/nix/store` drops below `min-free` during a
build, Lix performs a garbage-collection until `max-free` bytes are
available or there is no more garbage. A value of `0` (the default)
disables this feature.
