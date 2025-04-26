---
synopsis: "Always clean up scratch paths after derivations failed to build"
issues: []
cls: [3454]
category: "Fixes"
credits: ["raito", "horrors"]
---

Previously, scratch paths created during builds were not always cleaned up if
the derivation failed, potentially leaving behind unnecessary temporary files
or directories in the Nix store.

This fix ensures that such paths are consistently removed after a failed build,
improving Nix store hygiene, hardening Lix against mis-reuse of failed builds
scratch paths.
