---
name: getAttr
args: [s, set]
---
`getAttr` returns the attribute named *s* from *set*. Evaluation
aborts if the attribute doesn’t exist. This is a dynamic version of
the `.` operator, since *s* is an expression rather than an
identifier.

Has `O(log n)` time complexity, where `n` is number of attributes in the *set*.
