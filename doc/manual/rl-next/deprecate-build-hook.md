---
synopsis: "The `build-hook` setting is now deprecated"
category: Breaking Changes
---

Build hooks communicate with the daemon using a custom, internal, undocumented protocol that is entirely unversioned and cannot be changed.
Since we intend to change it anyway we must unfortunately deprecate the current build hook infrastructure.
We do not expect this to impact most users—we have not found any uses of `build-hook` in the wild—but if this does affect you, we'd like to hear from you!
