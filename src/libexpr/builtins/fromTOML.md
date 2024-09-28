---
name: fromTOML
args: [e]
renameInGlobalScope: false
---
Convert a TOML string to a Nix value. For example,

```nix
builtins.fromTOML ''
  x=1
  s="a"
  [table]
  y=2
''
```

returns the value `{ s = "a"; table = { y = 2; }; x = 1; }`.
