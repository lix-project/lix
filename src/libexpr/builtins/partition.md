---
name: partition
args: [pred, list]
---
Given a predicate function *pred*, this function returns an
attrset containing a list named `right`, containing the elements
in *list* for which *pred* returned `true`, and a list named
`wrong`, containing the elements for which it returned
`false`. For example,

```nix
builtins.partition (x: x > 10) [1 23 9 3 42]
```

evaluates to

```nix
{ right = [ 23 42 ]; wrong = [ 1 9 3 ]; }
```
