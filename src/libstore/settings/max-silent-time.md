---
name: max-silent-time
internalName: maxSilentTime
type: time_t
default: 0
aliases: [build-max-silent-time]
---
This option defines the maximum number of seconds that a builder can
go without producing any data on standard output or standard error.
This is useful (for instance in an automated build system) to catch
builds that are stuck in an infinite loop, or to catch remote builds
that are hanging due to network problems. It can be overridden using
the `--max-silent-time` command line switch.

The value `0` means that there is no timeout. This is also the
default.
