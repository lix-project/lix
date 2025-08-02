---
synopsis: "nix-eval-jobs: retain NIX_PATH"
issues: []
cls: [3859]
category: Fixes
credits: [ma27,mic92]
---

`nix-eval-jobs` doesn't clear the `NIX_PATH` from the environment anymore. This matches the behavior
of [upstream version `2.30`](https://github.com/nix-community/nix-eval-jobs/releases/tag/v2.30.0).
