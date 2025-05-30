---
synopsis: Allow `nix store ls` to read nar listings from binary cache stores.
issues: []
cls: [3225]
category: Improvements
credits: [vlinkz]
---

The `nix store ls` command now supports reading `.ls` nar listings from binary cache stores.
If a listing is detected for the store path being queried, the nar is no longer downloaded.
These nar listings are available in binary cache stores where the `write-nar-listing` option is
enabled, such as cache.nixos.org.
