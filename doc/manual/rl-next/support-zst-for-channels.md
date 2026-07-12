---
synopsis: "Support zstd and bz2 tarballs for Nix expressions in channels references"
cls: [5853, 5855]
category: "Features"
credits: [raito, horrors]
issues: [fj#1252]
---

Lix has always supported referencing NixOS channels using the `channel:`
syntax, e.g. `channel:nixos-26.05`. These references can be used anywhere
a source URL is expected, such as in `-I` flags with a Nix command or `url
= "channel:nixos-26.05";` in a `fetchTarball` call or even with the
`nix-channel` command.

Previously, such references resolved to a stable channel URL
(`https://channels.nixos.org/$VERSION`) and appended `nixexprs.tar.xz`. As the
Nixpkgs infrastructure has begun publishing `nixexprs.tar.zst` ([Zstandard](https://github.com/facebook/zstd))
files alongside existing formats, Lix now attempts to fetch tarballs in the
following priority order:

- `nixexprs.tar.zst` (Zstandard)
- `nixexprs.tar.xz` (XZ)
- `nixexprs.tar.bz2` (Bzip2)

This ensures that users can download historical and modern channel artifacts without any
change in their Nix code.
