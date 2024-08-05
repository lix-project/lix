---
name: auto-allocate-uids
internalName: autoAllocateUids
type: bool
default: false
experimentalFeature: auto-allocate-uids
---
Whether to select UIDs for builds automatically, instead of using the
users in `build-users-group`.

UIDs are allocated starting at 872415232 (0x34000000) on Linux and 56930 on macOS.
