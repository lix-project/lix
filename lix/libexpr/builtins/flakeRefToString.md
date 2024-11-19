---
name: flakeRefToString
implementation: flake::prim_flakeRefToString
args: [attrs]
experimentalFeature: flakes
---
Convert a flake reference from attribute set format to URL format.

For example:

```nix
builtins.flakeRefToString {
  dir = "lib"; owner = "NixOS"; ref = "23.05"; repo = "nixpkgs"; type = "github";
}
```

evaluates to

```nix
"github:NixOS/nixpkgs/23.05?dir=lib"
```
