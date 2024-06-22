---
synopsis: "`nix registry add` now requires a shorthand flakeref on the 'from' side"
cls: 1494
credits: delan
category: Improvements
---

The 'from' argument must now be a shorthand flakeref like `nixpkgs` or `nixpkgs/nixos-20.03`, making it harder to accidentally swap the 'from' and 'to' arguments.

Registry entries that map from other flake URLs can still be specified in registry.json, the `nix.registry` option in NixOS, or the `--override-flake` option in the CLI, but they are not guaranteed to work correctly.
