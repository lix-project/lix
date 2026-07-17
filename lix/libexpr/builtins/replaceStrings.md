---
name: replaceStrings
args: [from, to, s]
---
Given string *s*, replace every occurrence of the strings in *from*
with the corresponding string in *to*.

The argument *to* is lazy, that is, it is only evaluated when its corresponding pattern in *from* is matched in the string *s*

Example:

```nix
builtins.replaceStrings ["oo" "a"] ["a" "i"] "foobar"
```

evaluates to `"fabir"`.

Has `O(n k)` time complexity, where `n` is the length of *s* and `k` is the number of replacements.
