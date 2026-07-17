---
name: any
args: [pred, list]
---
Return `true` if the function *pred* returns `true` for at least one
element of *list*, and `false` otherwise.

Short-circuits and does not evaluate elements that appear later in the list if `pred` evaluates to `true`.
