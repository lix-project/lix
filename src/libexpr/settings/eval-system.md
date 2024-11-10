---
name: eval-system
internalName: currentSystem
type: std::string
default: ''
---
This option defines
[`builtins.currentSystem`](@docroot@/language/builtin-constants.md#builtins-currentSystem)
in the Nix language if it is set as a non-empty string.
Otherwise, if it is defined as the empty string (the default), the value of the
[`system` ](#conf-system)
configuration setting is used instead.

Unlike `system`, this setting does not change what kind of derivations can be built locally.
This is useful for evaluating Nix code on one system to produce derivations to be built on another type of system.
