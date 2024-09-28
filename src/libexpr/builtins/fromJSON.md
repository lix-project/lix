---
name: fromJSON
args: [e]
---
Convert a JSON string to a Nix value. For example,

```nix
builtins.fromJSON ''{"x": [1, 2, 3], "y": null}''
```

returns the value `{ x = [ 1 2 3 ]; y = null; }`.
