---
name: coerce-integers
internalName: CoerceIntegers
---
Automatically coerces integer values used in string interpolation to strings. This feature allows constructs like:

```nix
let
  version = 3;
in
  "v${version}"
```

to evaluate to:

```nix
"v3"
```

instead of producing a type error due to mismatched types in interpolation and hence requiring a call to `builtins.toString`.
