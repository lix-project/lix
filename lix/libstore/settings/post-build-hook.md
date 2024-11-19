---
name: post-build-hook
internalName: postBuildHook
type: std::string
default: ''
---
Optional. The path to a program to execute after each build.

This option is only settable in the global `nix.conf`, or on the
command line by trusted users.

When using the nix-daemon, the daemon executes the hook as `root`.
If the nix-daemon is not involved, the hook runs as the user
executing the nix-build.

  - The hook executes after an evaluation-time build.

  - The hook does not execute on substituted paths.

  - The hook's output always goes to the user's terminal.

  - If the hook fails, the build succeeds but no further builds
    execute.

  - The hook executes synchronously, and blocks other builds from
    progressing while it runs.

The program executes with no arguments. The program's environment
contains the following environment variables:

  - `DRV_PATH`
    The derivation for the built paths.

    Example:
    `/nix/store/5nihn1a7pa8b25l9zafqaqibznlvvp3f-bash-4.4-p23.drv`

  - `OUT_PATHS`
    Output paths of the built derivation, separated by a space
    character.

    Example:
    `/nix/store/zf5lbh336mnzf1nlswdn11g4n2m8zh3g-bash-4.4-p23-dev
    /nix/store/rjxwxwv1fpn9wa2x5ssk5phzwlcv4mna-bash-4.4-p23-doc
    /nix/store/6bqvbzjkcp9695dq0dpl5y43nvy37pq1-bash-4.4-p23-info
    /nix/store/r7fng3kk3vlpdlh2idnrbn37vh4imlj2-bash-4.4-p23-man
    /nix/store/xfghy8ixrhz3kyy6p724iv3cxji088dx-bash-4.4-p23`.
