---
args: ['--debugger']
---

we don't enter a debug repl through tryEval
```nix
(builtins.tryEval ((x: throw "foo") 1)).success
```
```output
false

```


no segfault either
```nix
:quit
```
```output

```
