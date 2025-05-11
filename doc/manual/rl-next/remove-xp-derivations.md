---
synopsis: Remove impure derivations and dynamic derivations
issues: [fj#815]
cls: [3210]
significance: significant
category: "Breaking Changes"
credits: [horrors]
---

The `impure-derivations` and `dynamic-derivations` experimental feature have
been removed.

New impure or dynamic derivations cannot be created from this point forward, and
any such pre-existing store derivations canot be read or built any more.
Derivation outputs created by building such a derivation are still valid
until garbage collected; existing store derivations can only be garbage
collected.
