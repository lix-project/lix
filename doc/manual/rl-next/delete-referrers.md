---
synopsis: "Fix nix-store --delete on paths with remaining referrers"
cls: [2783]
category: "Fixes"
credits: lheckemann
---

Nix 2.5 introduced a regression whereby `nix-store --delete` and `nix
store delete` started to fail when trying to delete a path that was
still referenced by other paths, even if the referrers were not
reachable from any GC roots. The old behaviour, where attempting to
delete a store path would also delete its referrer closure, is now
restored.
