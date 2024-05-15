---
synopsis: re-evaluate cached evaluation errors
cls: 771
credits: Qyriad
category: Fixes
---

"cached failure of [expr]" errors have been removed: expressions already in the
eval cache as a failure will now simply be re-evaluated, removing the need to
set `--no-eval-cache` or similar to see the error.
