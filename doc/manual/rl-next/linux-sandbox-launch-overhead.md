---
synopsis: "Linux sandbox launch overhead greatly reduced"
cls: [5030]
category: "Improvements"
credits: [horrors]
---

Sandboxed builds are now much cheaper to launch on Linux with 50% lower management
overhead. This will mostly be noticeable when building derivation trees containing
many small derivations like nixpkgs' `writeFile` or `runCommand` with scripts that
very quickly. In synthetic tests we have seen build times of 3000 small runCommand
drop from 80 seconds to 44 seconds, which is the most optimistic case in practice.
