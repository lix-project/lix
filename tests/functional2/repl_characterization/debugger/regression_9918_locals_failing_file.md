---
args: ['--debugger', '-f', 'regression_9918.nix']
should_fail: True
files: ['regression_9918.nix']
---

```output
Lix VERSION
Type :? for help.
error:
       … while evaluating the error message passed to builtin.throw

       error: cannot coerce a list to a string: [ ]

Added 2 variables.
```

We expect to be able to see locals like `r` in the debugger:

```nix
r
:env
```
```output
[ ]

Env level 0
static: r x 

Env level 1
abort baseNameOf break builtins derivation derivationStrict dirOf false fetchGit fetchMercurial fetchTarball fetchTree fromTOML import isNull map null placeholder removeAttrs scopedImport throw toString true 

```

```nix
:quit
```
```output
error:
       … while evaluating the file '/pwd/regression_9918.nix':

       … while evaluating x
         at /pwd/regression_9918.nix:5:3:
            4| in
            5|   x
             |   ^
            6|

       … while calling the 'throw' builtin
         at /pwd/regression_9918.nix:3:7:
            2|   r = [];
            3|   x = builtins.throw r;
             |       ^
            4| in

       … while evaluating the error message passed to builtin.throw

       error: cannot coerce a list to a string: [ ]
```
