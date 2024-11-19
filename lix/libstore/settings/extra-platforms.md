---
name: extra-platforms
internalName: extraPlatforms
type: StringSet
defaultExpr: 'getDefaultExtraPlatforms()'
defaultText: '*machine-specific*'
---
System types of executables that can be run on this machine.

Lix will only build a given [derivation](@docroot@/language/derivations.md) locally when its `system` attribute equals any of the values specified here or in the [`system` option](#conf-system).

Setting this can be useful to build derivations locally on compatible machines:
- `i686-linux` executables can be run on `x86_64-linux` machines (set by default)
- `x86_64-darwin` executables can be run on macOS `aarch64-darwin` with Rosetta 2 (set by default where applicable)
- `armv6` and `armv5tel` executables can be run on `armv7`
- some `aarch64` machines can also natively run 32-bit ARM code
- `qemu-user` may be used to support non-native platforms (though this
may be slow and buggy)

Build systems will usually detect the target platform to be the current physical system and therefore produce machine code incompatible with what may be intended in the derivation.
You should design your derivation's `builder` accordingly and cross-check the results when using this option against natively-built versions of your derivation.
