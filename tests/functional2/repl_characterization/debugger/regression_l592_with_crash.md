---
args: ['--debugger', '-f', 'regression_l592.nix']
should_fail: True
files: ['regression_l592.nix']
---

```output
Lix VERSION
Type :? for help.
info: breakpoint reached

Added 1 variables.
```
the with caused a crash with empty things
```nix
:quit
```

```output
error:
       … while evaluating the file '/pwd/regression_l592.nix':

       … while calling the 'seq' builtin
         at /pwd/regression_l592.nix:1:15:
            1| let x = 4; in __seq x (with x; (x: builtins.break x) 1)
             |               ^
            2|

       … from call site
         at /pwd/regression_l592.nix:1:32:
            1| let x = 4; in __seq x (with x; (x: builtins.break x) 1)
             |                                ^
            2|

       … while calling anonymous lambda
         at /pwd/regression_l592.nix:1:33:
            1| let x = 4; in __seq x (with x; (x: builtins.break x) 1)
             |                                 ^
            2|

       … while calling the 'break' builtin
         at /pwd/regression_l592.nix:1:36:
            1| let x = 4; in __seq x (with x; (x: builtins.break x) 1)
             |                                    ^
            2|

       error: breakpoint reached
```
