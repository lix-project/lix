---
synopsis: "flake config warnings are now printed to stderr"
issues: [1155]
cls: [5379]
category: "Fixes"
credits: [lheckemann]
---

The settings listed in a flake-config confirmation prompt are now printed to stderr rather than stdout, which allows `nix print-dev-env` to emit valid bash again even in the presence of untrusted settings.
