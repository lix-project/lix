---
synopsis: "allow setting nested attributes via `--arg`/`--argstr`"
cls: [5338]
category: "Features"
credits: [ma27]
issues: [fj#496]
---

Passing `--arg config.allowUnfree true` to e.g. `nix-build` now results in `config` with value
`{ allowUnfree = true; }` passed to the expression.
