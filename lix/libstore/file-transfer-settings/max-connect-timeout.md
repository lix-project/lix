---
name: max-connect-timeout
internalName: maxConnectTimeout
type: unsigned long
default: 300
aliases: [connect-timeout]
---
The maximum timeout for establishing connections for file transfers
such as tarball fetches or binary cache substitutions in seconds.
This is the maximum value Lix
sets for `curl`'s `--connect-timeout` option.

See [`initial-connect-timeout`](#conf-initial-connect-timeout)
for further information.
