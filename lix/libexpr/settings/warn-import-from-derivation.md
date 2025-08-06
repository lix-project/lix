---
name: warn-import-from-derivation
internalName: warnImportFromDerivation
type: bool
default: false
---

By default, Lix allows you to `import` from a derivation, allowing
building at evaluation time. With this option set to true, Lix will
throw a warning when evaluating an expression that uses this feature,
allowing users to know when their evaluation will requires any builds
to take place.

This option has no effect when `allow-import-from-derivation` is `false`.
