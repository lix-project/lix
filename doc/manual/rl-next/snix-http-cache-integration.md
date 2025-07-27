---
synopsis: "libstore/binary-cache-store: don't cache narinfo on nix copy, remove negative entry"
issues: []
cls: [3789]
category: Fixes
credits: [ma27]
---

When using e.g. [Snix's nar-bridge](https://snix.dev/docs/components/overview/#nar-bridge) via
an `http`-store, Lix would create cache entries with a wrong URL to the NAR when uploading
a store-path.

This caused hard build failures for Hydra.

Lix doesn't create these entries on upload anymore. Instead, it only removes negative cache entries.
The cache entry for a narinfo is now created the first time, Lix queries the cache
for the previously uploaded store-path again.
