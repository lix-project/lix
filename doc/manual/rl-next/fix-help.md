---
synopsis: "Fix `--help` formatting"
issues: [fj#622]
cls: [2776]
category: "Fixes"
credits: ["lheckemann"]
---

The help printed when invoking `nix` or `nix-store` and subcommands with `--help` previously contained garbled terminal escapes. These have been removed.
