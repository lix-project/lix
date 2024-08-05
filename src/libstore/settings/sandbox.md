---
name: sandbox
internalName: sandboxMode
type: SandboxMode
defaultExpr: |
  #if __linux__
    smEnabled
  #else
    smDisabled
  #endif
defaultText: '*Linux:* `true`, *other platforms:* `false`'
aliases: [build-use-chroot, build-use-sandbox]
---
If set to `true`, builds will be performed in a *sandboxed
environment*, i.e., they’re isolated from the normal file system
hierarchy and will only see their dependencies in the Nix store,
the temporary build directory, private versions of `/proc`,
`/dev`, `/dev/shm` and `/dev/pts` (on Linux), and the paths
configured with the `sandbox-paths` option. This is useful to
prevent undeclared dependencies on files in directories such as
`/usr/bin`. In addition, on Linux, builds run in private PID,
mount, network, IPC and UTS namespaces to isolate them from other
processes in the system (except that fixed-output derivations do
not run in private network namespace to ensure they can access the
network).

Currently, sandboxing only work on Linux and macOS. The use of a
sandbox requires that Lix is run as root (so you should use the
“build users” feature to perform the actual builds under different
users than root).

If this option is set to `relaxed`, then fixed-output derivations
and derivations that have the `__noChroot` attribute set to `true`
do not run in sandboxes.

The default is `true` on Linux and `false` on all other platforms.
