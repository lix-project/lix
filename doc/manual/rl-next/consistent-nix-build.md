---
synopsis: Show all FOD errors with `nix build --keep-going`
credits: [ma27]
category: Improvements
cls: [1108]
---

`nix build --keep-going` now behaves consistently with `nix-build --keep-going`. This means
that if e.g. multiple FODs fail to build, all hash mismatches are displayed.
