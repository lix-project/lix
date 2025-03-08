---
synopsis: 'Deletion of specific paths no longer fails fast'
issues: []
cls: [2778]
category: Improvements
credits: [lheckemann]
---

`nix-store --delete` and `nix store delete` now continue deleting
paths even if some of the given paths are still live. An error is only
thrown once deletion of all the given paths has been
attempted. Previously, if some paths were deletable and others
weren't, the deletable ones would be deleted iff they preceded the
live ones in lexical sort order.

The error message for still-live paths no longer reports the paths
that could not be deleted, because there could potentially be many of
these.
