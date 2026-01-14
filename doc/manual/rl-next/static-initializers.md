---
synopsis: "Plugin interfaces have changed (again)"
cls: [4933]
issues: [359]
category: "Miscellany"
credits: [horrors]
---

The `RegisterPrimOp` class used to register builtins has been removed. Plugins
must now call `PluginPrimOps::add` from their `nix_plugin_entry` with the same
parameters previously passed to `RegisterRrimOp` to register any new builtins.
