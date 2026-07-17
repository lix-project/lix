---
name: attrValues
args: [set]
---
Return the values of the attributes in the set *set* in the order
corresponding to the sorted attribute names.

Has `O(l n log n)` time complexity, where `n` is number of attributes in the *set* and `l` is the maximum attribute name length.
