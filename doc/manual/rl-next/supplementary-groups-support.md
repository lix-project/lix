---
synopsis: "Supplementary groups are now supported for daemon authentication"
cls: [5021]
issues: [fj#968]
category: "Improvements"
credits: [raito, thubrecht, alois31, horrors]
---

macOS, FreeBSD and Linux now support receiving supplementary groups during UNIX domain authentication to a Lix daemon.

This change is particularly beneficial for systemd units with `DynamicUser=true` that need to connect to a Lix daemon, using a `SupplementaryGroups=` allocated by systemd in the context of the process. This is desirable if you wish to harden Lix clients.
