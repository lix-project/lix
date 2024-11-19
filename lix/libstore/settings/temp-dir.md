---
name: temp-dir
internalName: tempDir
settingType: PathsSetting<std::optional<Path>>
default: null
---
The directory on the host used as the default temporary directory.

If not set, Nix will use the system temporary directory indicated by the `TMPDIR` environment variable.

This will be used for anything that would otherwise fall back to `TMPDIR`, and the inherited `TMPDIR` value will be preserved for child processes to use.
If [`build-dir`](#conf-build-dir) is set, that takes precedence over this where it applies.

If set, the value must be a path that exists and is accessible to all users.
