---
name: http-connections
internalName: httpConnections
type: size_t
default: 25
aliases: [binary-caches-parallel-connections]
---
The maximum number of parallel TCP connections used to fetch
files from binary caches and by other downloads. It defaults
to 25. 0 means no limit.
