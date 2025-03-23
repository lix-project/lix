---
synopsis: "Fix experimental and deprecated features showing as integers in `nix config show --json`"
issues: [fj#738]
cls: [2882]
category: "Fixes"
credits: ["horrors"]
---

Internal changes in 2.92 caused `nix config show --json` to show deprecated and experimental features not as the list of named features 2.91 and earlier produced, but as integers. This has been fixed.
