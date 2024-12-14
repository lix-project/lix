---
name: floating-without-zero
internalName: FloatingWithoutZero
timeline:
  - date: 2026-01-30
    release: 2.95.0
    cls: [2311]
    message: Introduced as warning.
---
The short-hand notation for floating point numbers `.123` instead of `0.123` is deprecated.
Floating point literals are seldomly used, and saving one character adds little benefit.
This deprecation will free the syntax for possible future new language feature (See NixOS RFC 181).

To fix this, add a zero before the dot.
