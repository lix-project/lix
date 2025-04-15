---
synopsis: "Remove lix-initiated ssh connection sharing"
issues: [fj#304, fj#644]
cls: [3005]
category: Fixes
credits: [horrors]
---

Lix no longer explicitly requests ssh connection sharing when connecting to remote stores.
This may impact command latency when `NIX_REMOTE` is set to a `ssh://` or `ssh-ng://` url,
or if `--store` is specified. Remote build connections did not use ssh connection sharing.

Connection sharing configuration is now inherited from user configuration at all times. It
is now advisable to configure connection sharing for remote builders for improved latency.
