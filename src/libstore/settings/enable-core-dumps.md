---
name: enable-core-dumps
internalName: enableCoreDumps
type: bool
default: false
---
If set to `false` (the default), `RLIMIT_CORE` has a soft limit of zero.
If set to `true`, the soft limit is infinite.

The hard limit is always infinite.
