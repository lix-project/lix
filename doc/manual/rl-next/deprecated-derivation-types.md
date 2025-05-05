---
synopsis: Deprecation of CA derivations, dynamic derivations, and impure derivations
issues: [fj#815]
cls: []
significance: significant
category: Miscellany
credits: []
---

Content-addressed derivations are now deprecated and slated for removal in Lix 2.94.
We're doing this because the CA derivation system has been a known cause of problems
and inconsistencies, is unmaintained, habitually makes improving the store code very
difficult (or blocks such improvements outright), and is beset by a number of design
flaws that in our opinion cannot be fixed without a full reimplementation from zero.
Dynamic derivations and impure derivations are built on the CA derivation framework,
and owing to this they too are deprecated and slated for removal in another release.
