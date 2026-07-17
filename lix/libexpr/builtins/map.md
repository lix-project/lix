---
name: map
args: [f, list]
renameInGlobalScope: false
---
Apply the function *f* to each element in the list *list*. For
example,

```nix
map (x: "foo" + x) [ "bar" "bla" "abc" ]
```

evaluates to `[ "foobar" "foobla" "fooabc" ]`.

Has `O(n)` time complexity, where `n` is the size of the *list*.
Note that no calls to *f* are performed by the builtin, but *f* itself is evaluated and its type is checked eagerly.
The function *f* is called on demand when a resulting list element is evaluated.
