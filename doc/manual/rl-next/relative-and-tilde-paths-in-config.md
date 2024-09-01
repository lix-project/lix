---
synopsis: Relative and tilde paths in configuration
issues: [fj#482]
cls: [1851, 1863, 1864]
category: Features
credits: [9999years]
---

[Configuration settings](@docroot@/command-ref/conf-file.md) can now refer to
files with paths relative to the file they're written in or relative to your
home directory (with `~/`).

This makes settings like
[`repl-overlays`](@docroot@/command-ref/conf-file.md#conf-repl-overlays) and
[`secret-key-files`](@docroot@/command-ref/conf-file.md#conf-repl-overlays)
much easier to set, especially if you'd like to refer to files in an existing
dotfiles repo cloned into your home directory.

If you put `repl-overlays = repl.nix` in your `~/.config/nix/nix.conf`, it'll
load `~/.config/nix/repl.nix`. Similarly, you can set `repl-overlays =
~/.dotfiles/repl.nix` to load a file relative to your home directory.

Configuration files can also
[`include`](@docroot@/command-ref/conf-file.md#file-format) paths relative to
your home directory.

Only user configuration files (like `$XDG_CONFIG_HOME/nix/nix.conf` or the
files listed in `$NIX_USER_CONF_FILES`) can use tilde paths relative to your
home directory. Configuration listed in the `$NIX_CONFIG` environment variable
may not use relative paths.
