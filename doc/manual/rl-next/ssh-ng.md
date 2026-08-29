---
synopsis: "`ssh-ng` stores support build options"
cls: [6415]
category: Features
credits: [horrors]
---

`ssh-ng` stores now support the options `build-timeout`, `max-silent-time`, and `keep-failed` when used with a compatible Lix.
This brings build option support to parity with `ssh` stores, particularly for remote builds.

Both involved Lixes must be 2.96 or newer for this to work.
An older Lix will never send these options, and a newer Lix will detect whether the remote advertises support for this feature and only then send these options.
