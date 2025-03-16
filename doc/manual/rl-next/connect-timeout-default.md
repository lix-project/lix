---
synopsis: Set default of `connect-timeout` to `5`
issues: []
cls: [2799]
category: Miscellany
credits: [ma27]
---

By default, the connection timeout to substituters is now 5s instead of 300s.
That way, unavailable substituters are detected quicker.
