---
synopsis: "`ssh-ng` stores support build options and are now considered stable"
cls: [6415, 6416]
category: Features
credits: [horrors]
---

`ssh-ng` stores now support the options `build-timeout`, `max-silent-time`, and `keep-failed` when used with a compatible Lix.
This brings build option support to parity with `ssh` stores, particularly for remote builds.

Both involved Lixes must be 2.96 or newer for this to work.
An older Lix will never send these options, and a newer Lix will detect whether the remote advertises support for this feature and only then send these options.

We now also consider `ssh-ng` stores as **stable, not experimental** any more.
The protocol has seen sufficient testing and the last remaining feature differences we know of are now resolved.
