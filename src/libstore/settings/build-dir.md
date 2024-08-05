---
name: build-dir
internalName: buildDir
settingType: PathsSetting<std::optional<Path>>
default: null
---
The directory on the host, in which derivations' temporary build directories are created.

If not set, Nix will use the [`temp-dir`](#conf-temp-dir) setting if set, otherwise the system temporary directory indicated by the `TMPDIR` environment variable.
Note that builds are often performed by the Nix daemon, so its `TMPDIR` is used, and not that of the Nix command line interface.

This is also the location where [`--keep-failed`](@docroot@/command-ref/opt-common.md#opt-keep-failed) leaves its files.

If Nix runs without sandbox, or if the platform does not support sandboxing with bind mounts (e.g. macOS), then the [`builder`](@docroot@/language/derivations.md#attr-builder)'s environment will contain this directory, instead of the virtual location [`sandbox-build-dir`](#conf-sandbox-build-dir).
