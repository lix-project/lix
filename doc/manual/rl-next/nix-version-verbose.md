---
synopsis: "`nix --version` now shows details about the installation by default"
category: Improvements
credits: [just1602]
cls: 2365
issues: [fj#620]
---

This happened with `nix-env --version` by default, but due to [oddities around the nix3 CLI's verbosity](https://gerrit.lix.systems/c/lix/+/1370), it used to be `nix --verbose --version`.

No longer:

```
$ nix --version
nix (Lix, like Nix) 2.92.0-dev-pre20250117-0d14c2b
System type: x86_64-linux
Additional system types: i686-linux, x86_64-v1-linux, x86_64-v2-linux, x86_64-v3-linux
Features: gc, signed-caches
System configuration file: /etc/nix/nix.conf
User configuration files: /home/jade/.config/nix/nix.conf:/etc/xdg/nix/nix.conf
Store directory: /nix/store
State directory: /nix/var/nix
Data directory: /nix/store/rliimcnqkplrqdgm4z6yqclpr6c32wh6-lix-2.92.0-dev-pre20250117-0d14c2b/share
```
