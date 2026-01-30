---
synopsis: "`impersonate-linux-26` setting removed"
cls: [5047]
category: "Miscellany"
credits: [horrors]
---

Linux 3.0 was released 15 years ago. The `impersonate-linux-26` setting was added
14 years ago with no mention of it being necessary to build anything, only saying
that it improves determinism—which isn't accurate since impersonating Linux 2.6.x
still allows the version string to change, and the final component of the version
does still change with each Linux release. Since this setting should be no longer
necessary in modern systems and workarounds for building old code exist (by using
e.g. `setarch --uname-2.6` to wrap builds) we are removing this setting from Lix.
