---
name: start-id
internalName: startId
type: uint32_t
defaultExpr: |
  #if __linux__
    0x34000000
  #else
    56930
  #endif
defaultText: '*Linux:* `872415232`, *other platforms:* `56930`'
experimentalFeature: auto-allocate-uids
---
The first UID and GID to use for dynamic ID allocation.
