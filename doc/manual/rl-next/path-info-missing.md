---
synopsis: "nix path-info no longer lies to the user about fetching paths"
cls: [4866]
issues: [fj#323]
category: "Improvements"
credits: [thubrecht]
---

When running `nix path-info` with an installable that is not present in the store, Lix no longer
tells the user which paths are missing and that they will be fetched, as the documentation clearly
states that this command does not fetch missing paths.
