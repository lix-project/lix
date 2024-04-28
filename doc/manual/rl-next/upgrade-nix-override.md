---
synopsis: add --store-path argument to `nix upgrade-nix`, to manually specify the Nix to upgrade to
cls: 953
---

`nix upgrade-nix` by default downloads a manifest to find the new Nix version to upgrade to, but now you can specify `--store-path` to upgrade Nix to an arbitrary version from the Nix store.
