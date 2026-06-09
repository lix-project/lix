---
args: ['--debugger', '-f', 'regression_l145.nix']
should_fail: True
files: ['regression_l145.nix']
---

```output
Lix VERSION
Type :? for help.
info: breakpoint reached

Added 1 variables.
```

debugger should not crash now, but also not show any `with` variables

```nix
:st
```
```output

0: error: breakpoint reached
/pwd/regression_l145.nix:3:7

     2| let
     3|   x = builtins.break 1;
      |       ^
     4| in

Env level 0
static: x 

Env level 1
static: 

Env level 2
abort baseNameOf break builtins derivation derivationStrict dirOf false fetchGit fetchMercurial fetchTarball fetchTree fromTOML import isNull map null placeholder removeAttrs scopedImport throw toString true 

Added 1 variables.

```

```nix
:quit
```
```output
error:
       … while evaluating the file '/pwd/regression_l145.nix':

       … while evaluating x
         at /pwd/regression_l145.nix:5:3:
            4| in
            5|   x
             |   ^
            6|

       … while calling the 'break' builtin
         at /pwd/regression_l145.nix:3:7:
            2| let
            3|   x = builtins.break 1;
             |       ^
            4| in

       error: breakpoint reached
```
