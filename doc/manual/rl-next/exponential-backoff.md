---
synopsis: "libstore: exponential backoff for downloads"
issues: [lix#932]
cls: [3856]
category: Fixes
credits: [ma27]
---

The connection timeout when downloading from e.g. a binary cache is exponentially
increased per failure. The option `connect-timeout` is now an alias to `max-connect-timeout`
which is the maximum value for a timeout. The start value is controlled
by `initial-connect-timeout` which is `5` by default.
