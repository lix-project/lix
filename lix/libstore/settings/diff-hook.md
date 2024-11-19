---
name: diff-hook
internalName: diffHook
settingType: PathsSetting<std::optional<Path>>
default: null
---
Path to an executable capable of diffing build results. The hook is
executed if `run-diff-hook` is true, and the output of a build is
known to not be the same. This program is not executed to determine
if two results are the same.

The diff hook is executed by the same user and group who ran the
build. However, the diff hook does not have write access to the
store path just built.

The diff hook program receives three parameters:

1.  A path to the previous build's results

2.  A path to the current build's results

3.  The path to the build's derivation

4.  The path to the build's scratch directory. This directory will
    exist only if the build was run with `--keep-failed`.

The stderr and stdout output from the diff hook will not be
displayed to the user. Instead, it will print to the nix-daemon's
log.

When using the Nix daemon, `diff-hook` must be set in the `nix.conf`
configuration file, and cannot be passed at the command line.
