---
synopsis: "nix-eval-jobs support `--select` flag"
cls: [5730]
category: "Features"
credits: [mic92]
---

`nix-eval-jobs` now supports the `--select` flag.
This flag allows to specify a function that is applied against the evaluation root.
This is applied before any attribute traversal begins.
