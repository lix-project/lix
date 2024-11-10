---
name: concatMap
args: [f, list]
---
This function is equivalent to `builtins.concatLists (map f list)`
but is more efficient.
