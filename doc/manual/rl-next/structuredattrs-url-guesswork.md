---
synopsis: "Hash mismatch diagnostics now work with `structuredAttrs`"
issues: [fj#1175]
cls: [5441]
category: Fixes
credits: [keysmashes]
---

Nixpkgs fetchers like `fetchurl` now use `structuredAttrs`, which broke the
hash mismatch diagnostics added in Lix 2.91. This has been fixed and the likely
URL is now shown again.
