---
name: max-build-log-size
internalName: maxLogSize
type: unsigned long
default: 0
aliases: [build-max-log-size]
---
This option defines the maximum number of bytes that a builder can
write to its stdout/stderr. If the builder exceeds this limit, it’s
killed. A value of `0` (the default) means that there is no limit.
