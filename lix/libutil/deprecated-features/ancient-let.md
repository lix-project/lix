---
name: ancient-let
internalName: AncientLet
timeline:
  - date: 2024-09-18
    release: 2.92.0
    cls: [1787]
    message: Introduced as soft deprecation with a warning.
  - date: 2026-01-29
    release: 2.95.0
    cls: [5039]
    message: Upgraded the warning to a parse error.
---
The ancient `let { body = …; … }` syntax is deprecated.

Use the `let … in` syntax instead.
