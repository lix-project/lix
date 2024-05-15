---
synopsis: using `nix profile` on `/nix/var/nix/profiles/default` no longer breaks `nix upgrade-nix`
cls: 952
credits: Qyriad
category: Fixes
---

On non-NixOS, Nix is conventionally installed into a `nix-env` style profile at /nix/var/nix/profiles/default.
Like any `nix-env` profile, using `nix profile` on it automatically migrates it to a `nix profile` style profile, which is incompatible with `nix-env`.
`nix upgrade-nix` previously relied solely on `nix-env` to do the upgrade, but now will work fine with either kind of profile.
