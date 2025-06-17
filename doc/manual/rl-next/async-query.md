---
synopsis: "Improved susbtituter query speed"
issues: []
cls: []
category: Improvements
credits: [horrors]
---

The code used to query substituters for derivations has been rewritten slightly
to take advantage of our asynchronous runtime. Such queries run for every build
that could download from substituters and processes every derivation that isn't
yet present on the local system. Previously Lix would use `http-connections` to
limit query concurrency, even for modern caches that support HTTP/2 and have no
limit on how many queries can be run concurrently on one single connection. Lix
no longer does this, resulting in approximately 60% reduction in query time for
medium-sized closures (e.g. NixOS system closures) during testing, although the
exact number depends greatly on local network latency and generally improves as
latency increases. Unlike previously setting `http-connections` to `1` or other
low values no longer brings a massive penalty in query performance if the cache
in use by the querying system supports HTTP/2 (as e.g. `cache.nixos.org` does).
