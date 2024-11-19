---
name: trace-function-calls
internalName: traceFunctionCalls
type: bool
default: false
---
If set to `true`, the Nix evaluator will trace every function call.
Nix will print a log message at the "vomit" level for every function
entrance and function exit.

    function-trace entered undefined position at 1565795816999559622
    function-trace exited undefined position at 1565795816999581277
    function-trace entered /nix/store/.../example.nix:226:41 at 1565795253249935150
    function-trace exited /nix/store/.../example.nix:226:41 at 1565795253249941684

The `undefined position` means the function call is a builtin.

Use the `contrib/stack-collapse.py` script distributed with the Nix
source code to convert the trace logs in to a format suitable for
`flamegraph.pl`.
