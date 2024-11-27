---
name: nixPath
type: list
implementation: prepareNixPath(searchPath)
---
The search path used to resolve angle bracket path lookups.

Angle bracket expressions can be
[desugared](https://en.wikipedia.org/wiki/Syntactic_sugar)
using this and
[`builtins.findFile`](./builtins.html#builtins-findFile):

```nix
<nixpkgs>
```

is equivalent to:

```nix
builtins.findFile builtins.nixPath "nixpkgs"
```
