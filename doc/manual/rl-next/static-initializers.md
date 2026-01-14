---
synopsis: "Plugin interfaces have changed (again)"
cls: [4933, 4934]
issues: [359]
category: "Miscellany"
credits: [horrors]
---

The `RegisterPrimOp` class used to register builtins has been removed. Plugins
must now call `PluginPrimOps::add` from their `nix_plugin_entry` with the same
parameters previously passed to `RegisterRrimOp` to register any new builtins.

The `GlobalConfig::Register` helper class has also been removed. Adding config
options to the system is now done with `GlobalConfig::registerGlobalConfig`; a
plugin can add config values by calling this function from `nix_plugin_entry`.
