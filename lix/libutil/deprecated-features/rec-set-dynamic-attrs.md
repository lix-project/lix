---
name: rec-set-dynamic-attrs
internalName: RecSetDynamicAttrs
timeline:
  - date: 2026-01-30
    release: 2.95.0
    cls: [4652, 5067]
    message: Introduced as a parser warning.
---
Dynamic attrs (attrs with interpolation in the key) are deprecated in `rec` attrsets.
This is because the recursive dynamics fundamentally do not work with dynamic attributes,
which is why dynamic attributes are currently evaluated *after* the recursive attributes, and *without* the recursive semantics.
