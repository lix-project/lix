---
name: experimental-features
internalName: experimentalFeatures
type: ExperimentalFeatures
default: []
---
Experimental features that are enabled.

Example:

```
experimental-features = nix-command flakes
```

The following experimental features are available:

{{#include @generated@/../../../lix/libutil/experimental-features-shortlist.md}}

Experimental features are [further documented in the manual](@docroot@/contributing/experimental-features.md).
