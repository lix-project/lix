---
args: ['--debugger', '--no-ignore-try']
---

we should enter a debug repl through `tryEval`

```nix
(builtins.tryEval ((x: throw "foo") 1)).success
```
```output
error: foo

This exception occurred in a 'tryEval' call. Use --ignore-try to skip these.

Added 1 variables.
```


no segfaults either
```nix
:quit
```
```output
error: foo

```
