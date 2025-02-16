---
name: substring
args: [start, len, s]
---
Return the substring of *s* from character position *start*
(zero-based) up to but not including *start + len*. If *start* is
greater than the length of the string, an empty string is returned,
and if *start + len* lies beyond the end of the string or *len*
is negative, only the substring up to the end of the string is
returned. *start* must be non-negative. For example,

```nix
builtins.substring 0 3 "nixos"
```

evaluates to `"nix"`, and

```nix
builtins.substring 3 (-1) "nixos"
```

evaluates to `"os"`.
