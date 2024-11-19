---
name: id-count
internalName: uidCount
type: uint32_t
defaultExpr: |
  #if __linux__
    maxIdsPerBuild * 128
  #else
    128
  #endif
defaultText: '*Linux:* `8388608`, *other platforms:* `128`'
experimentalFeature: auto-allocate-uids
---
The number of UIDs/GIDs to use for dynamic ID allocation.
