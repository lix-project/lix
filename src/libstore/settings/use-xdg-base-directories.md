---
name: use-xdg-base-directories
internalName: useXDGBaseDirectories
type: bool
default: false
---
If set to `true`, Lix will conform to the [XDG Base Directory Specification] for files in `$HOME`.
The environment variables used to implement this are documented in the [Environment Variables section](@docroot@/command-ref/env-common.md).

[XDG Base Directory Specification]: https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html

> **Warning**
> This changes the location of some well-known symlinks that Lix creates, which might break tools that rely on the old, non-XDG-conformant locations.

In particular, the following locations change:

| Old               | New                            |
|-------------------|--------------------------------|
| `~/.nix-profile`  | `$XDG_STATE_HOME/nix/profile`  |
| `~/.nix-defexpr`  | `$XDG_STATE_HOME/nix/defexpr`  |
| `~/.nix-channels` | `$XDG_STATE_HOME/nix/channels` |

If you already have Lix installed and are using [profiles](@docroot@/package-management/profiles.md) or [channels](@docroot@/command-ref/nix-channel.md), you should migrate manually when you enable this option.
If `$XDG_STATE_HOME` is not set, use `$HOME/.local/state/nix` instead of `$XDG_STATE_HOME/nix`.
This can be achieved with the following shell commands:

```sh
nix_state_home=${XDG_STATE_HOME-$HOME/.local/state}/nix
mkdir -p $nix_state_home
mv $HOME/.nix-profile $nix_state_home/profile
mv $HOME/.nix-defexpr $nix_state_home/defexpr
mv $HOME/.nix-channels $nix_state_home/channels
```
