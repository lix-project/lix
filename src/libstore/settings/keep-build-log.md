---
name: keep-build-log
internalName: keepLog
type: bool
default: true
aliases: [build-keep-log]
---
If set to `true` (the default), Lix will write the build log of a
derivation (i.e. the standard output and error of its builder) to
the directory `/nix/var/log/nix/drvs`. The build log can be
retrieved using the command `nix-store -l path`.
