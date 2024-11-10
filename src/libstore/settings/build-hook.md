---
name: build-hook
internalName: buildHook
type: Strings
default: []
deprecated: true
---
The path to the helper program that executes remote builds.

Lix communicates with the build hook over `stdio` using a custom protocol to request builds that cannot be performed directly by the Nix daemon.
The default value is the internal Lix binary that implements remote building.

> **Warning**
> Change this setting only if you really know what you’re doing.
