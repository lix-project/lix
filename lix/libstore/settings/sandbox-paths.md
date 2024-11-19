---
name: sandbox-paths
internalName: sandboxPaths
type: PathSet
default: []
aliases: [build-chroot-dirs, build-sandbox-paths]
---
A list of paths bind-mounted into Nix sandbox environments. You can
use the syntax `target=source` to mount a path in a different
location in the sandbox; for instance, `/bin=/nix-bin` will mount
the path `/nix-bin` as `/bin` inside the sandbox. If *source* is
followed by `?`, then it is not an error if *source* does not exist;
for example, `/dev/nvidiactl?` specifies that `/dev/nvidiactl` will
only be mounted in the sandbox if it exists in the host filesystem.

If the source is in the Nix store, then its closure will be added to
the sandbox as well.

Depending on how Lix was built, the default value for this option
may be empty or provide `/bin/sh` as a bind-mount of `bash`.
