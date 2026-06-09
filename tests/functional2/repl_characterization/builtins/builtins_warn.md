---
args: ['--debugger', '--option', 'debugger-on-warn', 'true']
---

Enter debugger with debugger-on-warn set.

```nix
let inspect = v: builtins.warn "inspect is deprecated" (throw "this happens after"); in inspect { }
```
```output
warning: inspect is deprecated
warning: builtins.warn reached

Added 2 variables.
```


Continue after the warn then breaks on the `throw`

```nix
:c
```
```output
error: this happens after

Added 2 variables.
```


Finally, continue after that error exits and prints the error
```nix
:c
```
```output
error: this happens after

```
