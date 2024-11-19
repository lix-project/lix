---
name: allow-import-from-derivation
internalName: enableImportFromDerivation
type: bool
default: true
---
By default, Lix allows you to `import` from a derivation, allowing
building at evaluation time. With this option set to false, Lix will
throw an error when evaluating an expression that uses this feature,
allowing users to ensure their evaluation will not require any
builds to take place.
