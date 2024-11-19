---
name: system
internalName: thisSystem
type: std::string
defaultExpr: 'SYSTEM'
defaultText: '*machine-specific*'
---
The system type of the current Lix installation.
Lix will only build a given [derivation](@docroot@/language/derivations.md) locally when its `system` attribute equals any of the values specified here or in [`extra-platforms`](#conf-extra-platforms).

The default value is set when Lix itself is compiled for the system it will run on.
The following system types are widely used, as [Lix is actively supported on these platforms](@docroot@/contributing/hacking.md#platforms):

- `x86_64-linux`
- `x86_64-darwin`
- `i686-linux`
- `aarch64-linux`
- `aarch64-darwin`
- `armv6l-linux`
- `armv7l-linux`

In general, you do not have to modify this setting.
While you can force Lix to run a Darwin-specific `builder` executable on a Linux machine, the result would obviously be wrong.

This value is available in the Nix language as
[`builtins.currentSystem`](@docroot@/language/builtin-constants.md#builtins-currentSystem)
if the
[`eval-system`](#conf-eval-system)
configuration option is set as the empty string.
