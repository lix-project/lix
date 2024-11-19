---
name: langVersion
type: integer
constructorArgs: ['NixInt{6}']
---
The legacy version of the Nix language. Always is `6` on Lix,
matching Nix 2.18.

Code in the Nix language should use other means of feature detection
like detecting the presence of builtins, rather than trying to find
the version of the Nix implementation, as there may be other Nix
implementations with different feature combinations.

If the feature you want to write compatibility code for cannot be
detected by any means, please file a Lix bug.
