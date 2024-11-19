---
name: compress-build-log
internalName: compressLog
type: bool
default: true
aliases: [build-compress-log]
---
If set to `true` (the default), build logs written to
`/nix/var/log/nix/drvs` will be compressed on the fly using bzip2.
Otherwise, they will not be compressed.
