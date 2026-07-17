---
name: attrNames
args: [set]
---
Return the names of the attributes in the set *set* in an
alphabetically sorted list. For instance, `builtins.attrNames { y
= 1; x = "foo"; }` evaluates to `[ "x" "y" ]`.

Has `O(l n log n)` time complexity, where `n` is number of attributes in the *set* and `l` is the maximum attribute name length.
