---
name: floor-ceil-corrupt-integers
internalName: FloorCeilCorruptIntegers
timeline:
  - date: 2026-04-30
    release: 2.96.0
    cls: [3923]
    message: Introduced as evaluation-time error.
---
Allow `builtins.floor` and `builtins.ceil` to corrupt integer inputs outside of the safe range to store in floats without precision loss (as in previous versions) rather than throwing an evaluation error.

In a future Lix release, `builtins.floor` and `builtins.ceil` will pass through integer inputs unchanged.

See: <https://github.com/NixOS/nix/issues/12899>.
