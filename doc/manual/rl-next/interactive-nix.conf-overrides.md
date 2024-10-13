---
synopsis: "Improvements to interactive flake config"
cls: [2066]
category: Improvements
credits: ma27
---

If `accept-flake-config` is set to `ask` and a `flake.nix` defines `nixConfig`,
Lix will ask on the CLI which of these settings should be used for the command.

Now, it's possible to answer with `N` (as opposed to `n` to only reject the setting
that is asked for) to reject _all untrusted_ entries from the flake's `nixConf`
section.
