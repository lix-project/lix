---
synopsis: Fix handling of truncated `.drv` files.
prs: 9673
category: Fixes
credits: horrors
---

Previously a `.drv` that was truncated in the middle of a string would case nix to enter an infinite loop, eventually exhausting all memory and crashing.
