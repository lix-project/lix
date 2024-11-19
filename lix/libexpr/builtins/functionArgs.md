---
name: functionArgs
args: [f]
---
Return a set containing the names of the formal arguments expected
by the function *f*. The value of each attribute is a Boolean
denoting whether the corresponding argument has a default value. For
instance, `functionArgs ({ x, y ? 123}: ...) = { x = false; y =
true; }`.

"Formal argument" here refers to the attributes pattern-matched by
the function. Plain lambdas are not included, e.g. `functionArgs (x:
...) = { }`.
