---
name: build-dir
internalName: buildDir
settingType: PathsSetting<Path>
defaultText: "`«nixStateDir»/builds`"
defaultExpr: nixStateDir + "/builds"
---
The directory on the host, in which derivations' temporary build directories are created.

If not set, Lix will use the `builds` subdirectory of its configured state directory.
Lix will create this directory automatically with suitable permissions if it does not
exist, otherwise its permissions must allow all users to traverse the directory (i.e.
it must have `o+x` set, in unix parlance) for non-sandboxed builds to work correctly.

This is also the location where [`--keep-failed`](@docroot@/command-ref/opt-common.md#opt-keep-failed) leaves its files.

If Nix runs without sandbox, or if the platform does not support sandboxing with bind mounts (e.g. macOS), then the [`builder`](@docroot@/language/derivations.md#attr-builder)'s environment will contain this directory, instead of the virtual location [`sandbox-build-dir`](#conf-sandbox-build-dir).

> Important:
>
> `build-dir` must not be set to a world-writable directory. Placing temporary build
> directories in a world-writable place allows other users to access or modify build
> data that is currently in use. This alone is merely an impurity, but combined with
> another factor this has allowed malicious derivations to escape the build sandbox.
