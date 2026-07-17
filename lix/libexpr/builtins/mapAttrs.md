---
name: mapAttrs
args: [f, attrset]
---
Apply function *f* to every element of *attrset*. For example,

```nix
builtins.mapAttrs (name: value: value * 10) { a = 1; b = 2; }
```

evaluates to `{ a = 10; b = 20; }`.

Has `O(n)` time complexity, where `n` is the size of the *attrset*.
Note that no calls to *f* are performed by the builtin.
The function *f* is called on demand when a resulting attribute value is evaluated.
