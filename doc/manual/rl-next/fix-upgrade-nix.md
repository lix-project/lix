---
synopsis: "Fix upgrade-nix breaking its own access to the daemon"
cls: [5504, 5567]
category: "Fixes"
issues: [fj#1189, fj#1207]
credits: [Qyriad]
---

`nix upgrade-nix`, and the helper script `misc/upgrade-lix.sh` now pass `--store local` to all Nix commands, so the upgrade process can make changes to the daemon without breaking further steps in the upgrade.
