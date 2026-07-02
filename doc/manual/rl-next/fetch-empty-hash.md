---
synopsis: "don't treat tarball fetches with empty or zero hash as locked"
cls: []
category: "Fixes"
credits: [horrors]
issues: [fj#1233]
---

Lix no longer treats tarball fetches with empty or zero hashes as locked.
All such fetches are now also affected by `tarball-ttl` as a consequence.
