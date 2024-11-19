---
name: traceVerbose
implementation: 'evalSettings.traceVerbose ? prim_trace : prim_second'
args: [e1, e2]
---
Evaluate *e1* and print its abstract syntax representation on standard
error if `--trace-verbose` is enabled. Then return *e2*. This function
is useful for debugging.
