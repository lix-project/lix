---
synopsis: Lix now enables parallel marking in boehm-gc
issues: [fj#983]
cls: [3880]
category: Improvements
credits: [edolstra, getchoo]
---

This brings a fairly modest performance improvement (~38% for `nixpkgs search hello`) to evaluation, especially in scenarios that necessitate larger heap sizes.
