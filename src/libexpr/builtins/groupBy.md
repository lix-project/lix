---
name: groupBy
args: [f, list]
---
Groups elements of *list* together by the string returned from the
function *f* called on each element. It returns an attribute set
where each attribute value contains the elements of *list* that are
mapped to the same corresponding attribute name returned by *f*.

For example,

```nix
builtins.groupBy (builtins.substring 0 1) ["foo" "bar" "baz"]
```

evaluates to

```nix
{ b = [ "bar" "baz" ]; f = [ "foo" ]; }
```
