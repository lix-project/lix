---
synopsis: "Add an option `always-allow-substitutes` to ignore `allowSubstitutes` in derivations"
prs: 8047
credits: [lovesegfault, horrors]
category: Improvements
---

You can set this setting to force a system to always allow substituting even
trivial derivations like `pkgs.writeText`. This is useful for
[`nix-fast-build --skip-cached`][skip-cached] and similar to be able to also
ignore trivial derivations.

[skip-cached]: https://github.com/Mic92/nix-fast-build?tab=readme-ov-file#avoiding-redundant-package-downloads
