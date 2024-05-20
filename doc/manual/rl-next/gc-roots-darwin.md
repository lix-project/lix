---
synopsis: Find GC roots using libproc on Darwin
cls: 723
credits: artemist
category: Improvements
---

Previously, the garbage collector found runtime roots on Darwin by shelling out to `lsof -n -w -F n` then parsing the result. The version of `lsof` packaged in Nixpkgs is very slow on Darwin, so Lix now uses `libproc` directly to speed up GC root discovery, in some tests taking 250ms now instead of 40s.
