---
synopsis: "Subdaemons gets overridden settings from their parent daemon"
cls: [6525]
category: "Fixes"
credits: [raito]
issues: [fj#1071, fj#1270]
---

Since 1e8f7c7c7, subdaemons have ceased to receive overridden settings from
their parent daemon, i.e. set by the CLI and not the configuration file.

This was not remarked because we have migrated most of the Lix users to
systemd-spawned subdaemons where the override is applied at the systemd
template unit.
