---
synopsis: "nix-eval-jobs: support `--no-instantiate` flag"
issues: [fj#987]
category: Features
credits: [mic92,ma27]
---

`nix-eval-jobs` now supports a flag called `--no-instantiate`. With this enabled,
no write operations on the eval store are performed. That means, only evaluation is
performed, but derivations (and their gcroots) aren't created.
