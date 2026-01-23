---
name: nix-path-shadow
internalName: NixPathShadow
timeline:
  - date: 2025-11-23
    release: 2.95.0
    cls: [4632]
    message: Introduced as an evaluation-time warning.
---

Shadowing `<nix/fetchurl.nix>` by configuring the [*nix path*](@docroot@/language/builtin-constants.html#builtins-nixPath) to a value containing `nix=/some/path` is deprecated.
The namespace `nix` in the Nix path is reserved for usage in internal code, and overriding it may result in nontrivial breakage.
