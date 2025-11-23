---
synopsis: "Deprecate shadowing internal files through the Nix search path"
issues: [998]
cls: [4632]
category: "Breaking Changes"
credits: [thubrecht]
---

As Lix uses the path `<nix/fetchurl.nix>` for bootstrapping purposes, the ability to shadow it by adding `nix=/some/path` (or `/other/path` that contains a `nix` directory) to the search path is not desirable.

To alleviate potential issues, Lix now emits a warning when the Nix search path contains potential shadows for internal files, which will be changed to an error in a future release.

The warning can be disabled by enabling the deprecated feature `nix-path-shadow`.
