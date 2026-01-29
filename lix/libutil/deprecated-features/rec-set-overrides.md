---
name: rec-set-overrides
internalName: RecSetOverrides
timeline:
  - date: 2024-09-18
    release: 2.92.0
    cls: [1744]
    message: Introduced as soft deprecation with a warning.
  - date: 2026-01-29
    release: 2.95.0
    cls: [5040]
    message: Upgraded the warning to a parse error.
---
The magic symbol `__overrides` in recursive attribute sets is deprecated.
It was introduced in the early days of the language before the widespread use of overlays and is not needed anymore.

To fix this, use fix point functions (e.g. `lib.fix` in Nixpkgs) instead.
