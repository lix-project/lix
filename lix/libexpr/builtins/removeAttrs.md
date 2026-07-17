---
name: removeAttrs
args: [set, list]
renameInGlobalScope: false
---
Remove the attributes listed in *list* from *set*. The attributes
don’t have to exist in *set*. For instance,

```nix
removeAttrs { x = 1; y = 2; z = 3; } [ "a" "x" "z" ]
```

evaluates to `{ y = 2; }`.

Has `O(n + k log k)` time complexity, where `n` is number of attributes in the *set* and `k` is the size of *list*.
