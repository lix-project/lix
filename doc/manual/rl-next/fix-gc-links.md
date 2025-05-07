---
synopsis: "nix-store --delete: always remove obsolete hardlinks"
issues: []
cls: [3188]
category: Fixes
credits: [lheckemann]
---

Deleting specific paths using `nix-store --delete` or `nix store
delete` previously did not delete hard links created by `nix-store
--optimise` even if they became obsolete, unless _all_ of the given
paths were deleted successfully. Now, hard links are always cleaned
up, even if some of the given paths could not be deleted.
