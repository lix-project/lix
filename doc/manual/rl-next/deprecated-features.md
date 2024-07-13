---
synopsis: Deprecated URL literals
issues: [fj#437]
cls: [1736, 1735, 1744]
category: Breaking Changes
credits: [piegames, horrors]
---

URL literals have long been obsolete and discouraged of use, and now they are officially deprecated.
This means that all URLs must be properly put within quotes like all other strings.

To ease migration, they can still be enabled with `--extra-deprecated-features url-literals` for now.
