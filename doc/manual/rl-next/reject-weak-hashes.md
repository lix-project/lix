---
synopsis: "Weak hash algorithms are now rejected in SRI form, and cause a warning otherwise"
category: Breaking Changes
credits: tcmal
cls: [2110]
issues: [8982, fj#114]
---

MD5 and SHA-1 algorithms are now no longer allowed in SRI form, as specified in [the spec](https://w3c.github.io/webappsec-subresource-integrity/#hash-functions).

These hash types will also give a warning when used in other cases.
