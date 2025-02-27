---
synopsis: Avoid unnecessarily killing processes for the build user's UID
issues: [nix#9142, fj#667]
cls: []
category: Fixes
credits: [teofilc]
---

We no longer kill all processes under the build user's UID before and after
builds on Linux with sandboxes enabled.

This avoids unrelated processes being killed. This might happen for instance,
if the user is running Lix inside a container, wherein the build users use the same UIDs as the daemon's.
