---
synopsis: "`nix eval --write-to` has been removed"
cls: [4045]
issues: [fj#974, fj#227]
category: "Breaking Changes"
credits: [horrors]
---

`nix eval --write-to` has been removed since it was underspecified, not widely
useful, and prone to security-sensitive misbehaviors. The feature was added in
Nix 2.4 purely for internal use in the build system. According to our research
it hasn't found any use outside of some distribution packaging scripts. Please
use structured outputs formats (such as JSON) instead as they have better type
fidelity, don't conflate attributes with paths, and are useful to other tools.
