---
synopsis: "Fix client-supplied file-transfer settings being silently ignored"
cls: [6537, 6539]
category: Fixes
credits: [raito]
issues: []
---

Client settings received by the daemon and the `build-remote` helper were
applied to the global `libstore` configuration only. Settings owned by other
configurations, e.g. `file-transfer` settings such as `max-connect-timeout`
and `initial-connect-timeout` were silently ignored.

These settings are now applied through the global configuration registry, which
dispatches each name to the configuration that owns it. As a side effect, this
also stops remote builds from dropping every non-`libstore` setting received
from its parent.
