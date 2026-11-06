---
synopsis: "Preserve daemon log severity over the worker protocol"
category: Fixes
---

Updated clients and daemons preserve log levels over local and `ssh-ng://`
connections, including the worker protocol's initial diagnostics, so warnings no longer arrive as errors.
Connections to older peers retain the legacy behavior.
