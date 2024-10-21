---
synopsis: Add a `temp-dir` setting to set the temporary directory location
issues: [7731, 8995, fj#112, fj#253]
cls: [2103]
category: Improvements
credits: lilyball
---

[`temp-dir`](@docroot@/command-ref/conf-file.md#conf-temp-dir) can now be set in the Nix
configuration to change the temporary directory. This can be used to relocate all temporary files
to another filesystem without affecting the `TMPDIR` env var inherited by interactive
`nix-shell`/`nix shell` shells or `nix run` commands.

Also on macOS, the `TMPDIR` env var is no longer unset for interactive shells when pointing
to a per-session `/var/folders/` directory.
