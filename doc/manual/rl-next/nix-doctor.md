---
synopsis: "Improve nix doctor"
cls: [5316, 5317, 5318, 5319, 5320, 5768, 5829]
category: Features
credits: [rootile, raito]
---
The `nix doctor` diagnosics interface now provides a lot more useful information including, but not limited to:
- General system information (OS, Hardware etc)
- Nix Information like Sandbox, Version, Store, State and other directories
- Flake registry
- Search path Information
- Nixpkgs provenance
- Remote builder configuration (including remote connection)
- fix crash when having relative Paths in PATH
