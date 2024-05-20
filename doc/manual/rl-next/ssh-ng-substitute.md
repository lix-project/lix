---
synopsis: Fix `ssh-ng://` remotes not respecting `--substitute-on-destination`
prs: 9600
credits: SharzyL
category: Fixes
---

`nix copy ssh-ng://` now respects `--substitute-on-destination`, as does `nix-copy-closure` and other commands that operate on remote `ssh-ng` stores.
Previously this was always set by `builders-use-substitutes` setting.
