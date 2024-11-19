---
name: run-diff-hook
internalName: runDiffHook
type: bool
default: false
---
If true, enable the execution of the `diff-hook` program.

When using the Nix daemon, `run-diff-hook` must be set in the
`nix.conf` configuration file, and cannot be passed at the command
line.
