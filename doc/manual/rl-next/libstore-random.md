---
synopsis: Fix potential store corruption with auto-optimise-store
issues: [7273]
cls: [2100]
category: Fixes
credits: lilyball
---

Optimising store paths (and other operations involving temporary files) no longer use `random(3)`
to generate filenames. On darwin systems this was observed to potentially cause store corruption
when using [`auto-optimise-store`](@docroot@/command-ref/conf-file.md#conf-auto-optimise-store),
though this corruption was possible on any system whose `random(3)` does not have locking around
the global state.
