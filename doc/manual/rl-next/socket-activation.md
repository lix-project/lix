---
synopsis: "Lix daemons are now fully socket-activated on systemd setups"
cls: []
issues: [1030]
category: "Miscellany"
credits: [horrors]
---

When launched by systemd, Lix no longer uses a persistent daemon process and uses systemd socket
activation instead. This is necessary to support the `cgroups` and `auto-allocate-uids` features
and may improve observability of daemon behavior with common systemd-based monitoring solutions.

The old behavior with a single persistent daemon is still available, but disabled by default. It
is not possible to enable both a persistent daemon and socket activation, starting one stops the
other automatically. Existing installations should not require any changes when they're updated.
