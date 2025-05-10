---
synopsis: Remove impure derivations
issues: [fj#815]
cls: [3210]
significance: significant
category: "Breaking Changes"
credits: [horrors]
---

The `impure-derivations` experimental feature has been removed. New impure
derivations cannot be created from this point forward, and existing impure store
derivations canot be read or built any more. Derivation outputs created by
building an impure derivation are still valid until garbage collected; existing
impure store derivations can only be garbage collected.
