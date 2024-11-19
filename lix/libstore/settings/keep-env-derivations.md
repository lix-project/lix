---
name: keep-env-derivations
internalName: envKeepDerivations
type: bool
default: false
aliases: [env-keep-derivations]
---
If `false` (default), derivations are not stored in Nix user
environments. That is, the derivations of any build-time-only
dependencies may be garbage-collected.

If `true`, when you add a Nix derivation to a user environment, the
path of the derivation is stored in the user environment. Thus, the
derivation will not be garbage-collected until the user environment
generation is deleted (`nix-env --delete-generations`). To prevent
build-time-only dependencies from being collected, you should also
turn on `keep-outputs`.

The difference between this option and `keep-derivations` is that
this one is “sticky”: it applies to any user environment created
while this option was enabled, while `keep-derivations` only applies
at the moment the garbage collector is run.
