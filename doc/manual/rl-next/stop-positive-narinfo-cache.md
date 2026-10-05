---
synopsis: "Stop persisting positive narinfo lookups in the disk cache"
cls: [6519]
category: "Breaking Changes"
credits: [raito, horrors]
issues: []
---

The on-disk narinfo cache (`$XDG_CACHE_HOME/nix/binary-cache-v6.sqlite`) no
longer stores positive lookups. It still caches negative lookups and the
per-cache `/nix-cache-info` metadata (store directory, priority).

## Why

A positive entry recorded a full `.narinfo` (NAR URL, hashes, size and
signatures) and was reused for `narinfo-cache-positive-ttl` (30 days by
default).

When a cache later re-signed a path, moved its NAR, garbage-collected and
rebuilt it, or was reconfigured, Lix kept replaying the old metadata and failed
with confusing, persistent errors, until the entry expired.

Negative and metadata entries are self-correcting and do not have a similar
impact.

## Consequences

**Traffic** : A process that substitutes a path now fetches its `.narinfo` once (still cached
in-memory) instead of reading it from disk. Re-substituting a closure of
N paths after a GC therefore costs up to N additional small HTTP GETs per
process. Long-running invocations still avoid repeated lookups through the
in-process positive cache.

**Latency** : The added cost is roughly `N × RTT / parallel substitution job`,
for a 2k path closure on a 100 ms link, we expect an added time of ~10s.

Tests over the texlive package set (large closure set of 5k paths) did not show
meaningful differences in the delays over a relatively fast link.
