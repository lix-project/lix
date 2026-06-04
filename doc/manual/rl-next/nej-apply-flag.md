---
synopsis: "nix-eval-jobs support `--apply` flag"
cls: [5748]
category: "Features"
credits: [isabelroses,mic92,ysndr]
issues: [fj#1214]
---

`nix-eval-jobs` now supports the `--apply` flag. With this you can apply the
provided function to the each derivation, the result of this function will then
be serialized as a JSON value and stored inside `"extraValue"` key of the json
line output.
