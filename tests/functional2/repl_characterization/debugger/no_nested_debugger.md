---
args: ['--debugger', '-f', 'no_nested_debuggers.nix']
files: ['no_nested_debuggers.nix']
---

```output
Lix VERSION
Type :? for help.
info: breakpoint reached

```
we enter a debugger via builtins.break in the input file.

```nix
"values show"
```
```output
"values show"

```


causing another debugger even should not nest, but simply print the error, skip the breakpoint, etc as appropriate

```nix
builtins.break 2
builtins.throw "foo"
assert false; 2
```
```output
2

error:
       … caused by explicit throw
         at «string»:1:1:
            1| builtins.throw "foo"
             | ^

       error: foo

error: assertion failed
       at «string»:1:1:
            1| assert false; 2
             | ^

```


exiting the debugger frame should allow another to open.

```nix
:c
builtins.throw "bar"
```
```output
Loading installable ''...
error: bar

```


and once again, more breakpoints are ignored.

```nix
builtins.break 3
```
```output
3

```
