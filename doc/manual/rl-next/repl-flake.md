---
synopsis: Remove experimental repl-flake
issues: [gh#10103, fj#557]
cls: [2147]
prs: [gh#10299]
category: Breaking Changes
credits: [detroyejr, kfears]
---

The `repl-flake` experimental feature flag has been removed, its functionality is now the default when `flakes` experimental feature is active. The `nix repl` command now works like the rest of the new CLI in that `nix repl {path}` now tries to load a flake at `{path}` (or fails if the `flakes` experimental feature isn't enabled).
