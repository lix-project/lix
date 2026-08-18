---
synopsis: "`nix-hash` can no longer truncate SRI hashes"
cls: []
category: Fixes
credits: [horrors]
---

`nix-hash` was able to truncate SRI hashes using its `--truncate` flag, creating invalid SRI hashes in the process. This is no longer allowed.
