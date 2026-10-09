---
synopsis: "Forbid unprivileged `repair` through the daemon"
issues: []
cls: []
category: "Fixes"
credits: [raito]
---

The daemon's `AddToStore`, `AddToStoreNar` and `AddMultipleToStore` operations
accepted the client-supplied `repair` flag even from untrusted clients. Because
`LocalStore` does not repair paths atomically, an untrusted client could
replace the contents of an arbitrary store path with attacker-controlled data
while the path remained registered as valid in the database, leading to local
privilege escalation.

`repair` is now rejected for untrusted clients, matching the existing
restrictions on `BuildPaths`, `BuildPathsWithResults` and `VerifyStore`.
