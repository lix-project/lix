A previously unforced thunk is an attribute set does not lead to indentation when it won't evalutate to a neted structure:

```nix
:p let x = 1 + 2; in [ { inherit x; } { inherit x; } ]
```
```output
[
  { x = 3; }
  { x = 3; }
]

```


Same for a list:

```nix
:p let x = 1 + 2; in [ [ x ] [ x ]]
```
```output
[
  [ 3 ]
  [ 3 ]
]

```
