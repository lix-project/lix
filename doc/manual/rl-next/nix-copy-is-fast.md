---
synopsis: "`nix copy` is now several times faster at `querying info about /nix/store/...`"
cls: [1462]
issues: [fj#366]
credits: [jade]
category: Fixes
---

We fixed a locking bug that serialized `querying info about /nix/store/...`
onto just one thread such that it was eating `O(paths to copy * latency)` time
while setting up to copy paths to s3 and other stores. It is now `nproc` times
faster.
