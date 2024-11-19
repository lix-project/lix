---
name: use-case-hack
internalName: useCaseHack
type: bool
defaultExpr: |
  #if __APPLE__
    true
  #else
    false
  #endif
defaultText: '*Darwin:* `true`, *other platforms:* `false`'
---
Whether to enable a Darwin-specific hack for dealing with file name collisions.
