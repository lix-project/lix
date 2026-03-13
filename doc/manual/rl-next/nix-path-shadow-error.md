---
synopsis: "Shadowing internal files through the Nix search path is now an error"
issues: [998]
cls: [4632, 5370]
category: "Breaking Changes"
credits: [thubrecht, jade, horrors]
---

As Lix uses the path `<nix/fetchurl.nix>` for bootstrapping purposes, the ability to shadow it by adding `nix=/some/path` (or `/other/path` that contains a `nix` directory) to the search path is not desirable.

Lix 2.95 deprecated this behavior with a warning, Lix 2.96 now turns it into a hard error if the `nix-path-shadow` deprecated feature isn't enabled. This deprecated feature is slated to be removed in Lix 2.98.
