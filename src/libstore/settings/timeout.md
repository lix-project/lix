---
name: timeout
internalName: buildTimeout
type: time_t
default: 0
aliases: [build-timeout]
---
This option defines the maximum number of seconds that a builder can
run. This is useful (for instance in an automated build system) to
catch builds that are stuck in an infinite loop but keep writing to
their standard output or standard error. It can be overridden using
the `--timeout` command line switch.

The value `0` means that there is no timeout. This is also the
default.
