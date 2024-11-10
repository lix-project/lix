---
name: nixVersion
type: string
constructorArgs: ['"2.18.3-lix"']
---
Legacy version of Nix. Always returns "2.18.3-lix" on Lix.

Code in the Nix language should use other means of feature detection
like detecting the presence of builtins, rather than trying to find
the version of the Nix implementation, as there may be other Nix
implementations with different feature combinations.

If the feature you want to write compatibility code for cannot be
detected by any means, please file a Lix bug.
