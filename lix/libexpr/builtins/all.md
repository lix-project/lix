---
name: all
args: [pred, list]
---
Return `true` if the function *pred* returns `true` for all elements
of *list*, and `false` otherwise.

Short-circuits and does not evaluate elements that appear later in the list if `pred` evaluates to `false`.
