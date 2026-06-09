---
args: ['--debugger', '-f', '{PWD}/stack_vars.nix']
should_fail: True
files: ['stack_vars.nix']
---

```output
Lix VERSION
Type :? for help.
trace: before outer break
info: breakpoint reached

Added 3 variables.
```
Here we are in the outer break and the let of "meow". `:st` should show meow there
```nix
:st
```
```output

0: error: breakpoint reached
/pwd/stack_vars.nix:6:22

     5|   b = builtins.trace "before outer break" (
     6|     let meow = 2; in builtins.break a
      |                      ^
     7|   );

Env level 0
static: meow 

Env level 1
static: a b 

Env level 2
abort baseNameOf break builtins derivation derivationStrict dirOf false fetchGit fetchMercurial fetchTarball fetchTree fromTOML import isNull map null placeholder removeAttrs scopedImport throw toString true 

Added 3 variables.

```
```nix
meow
```
```output
2

```




If we `:st` past the frame in the backtrace with the `meow` in it, the `meow` should not be there
```nix
:st 3
:c
```
```output

3: while calling a function
/pwd/stack_vars.nix:5:7

     4|   );
     5|   b = builtins.trace "before outer break" (
      |       ^
     6|     let meow = 2; in builtins.break a

Env level 0
static: a b 

Env level 1
abort baseNameOf break builtins derivation derivationStrict dirOf false fetchGit fetchMercurial fetchTarball fetchTree fromTOML import isNull map null placeholder removeAttrs scopedImport throw toString true 

Added 2 variables.

trace: before inner break
info: breakpoint reached

Added 3 variables.
```

```nix
:st
```
```output

0: error: breakpoint reached
/pwd/stack_vars.nix:3:23

     2|   a = builtins.trace "before inner break" (
     3|     let meow' = 3; in builtins.break { msg = "hello"; }
      |                       ^
     4|   );

Env level 0
static: meow' 

Env level 1
static: a b 

Env level 2
abort baseNameOf break builtins derivation derivationStrict dirOf false fetchGit fetchMercurial fetchTarball fetchTree fromTOML import isNull map null placeholder removeAttrs scopedImport throw toString true 

Added 3 variables.

```

```nix
meow'
:st 3
```
```output
3


3: while calling a function
/pwd/stack_vars.nix:2:7

     1| let
     2|   a = builtins.trace "before inner break" (
      |       ^
     3|     let meow' = 3; in builtins.break { msg = "hello"; }

Env level 0
static: a b 

Env level 1
abort baseNameOf break builtins derivation derivationStrict dirOf false fetchGit fetchMercurial fetchTarball fetchTree fromTOML import isNull map null placeholder removeAttrs scopedImport throw toString true 

Added 2 variables.

```



```nix
:quit
```
```output
error:
       … while evaluating the file '/pwd/stack_vars.nix':

       … while evaluating b
         at /pwd/stack_vars.nix:9:3:
            8| in
            9|   b
             |   ^
           10|

       … while calling the 'trace' builtin
         at /pwd/stack_vars.nix:5:7:
            4|   );
            5|   b = builtins.trace "before outer break" (
             |       ^
            6|     let meow = 2; in builtins.break a

       … while calling the 'break' builtin
         at /pwd/stack_vars.nix:6:22:
            5|   b = builtins.trace "before outer break" (
            6|     let meow = 2; in builtins.break a
             |                      ^
            7|   );

       … while calling the 'trace' builtin
         at /pwd/stack_vars.nix:2:7:
            1| let
            2|   a = builtins.trace "before inner break" (
             |       ^
            3|     let meow' = 3; in builtins.break { msg = "hello"; }

       … while calling the 'break' builtin
         at /pwd/stack_vars.nix:3:23:
            2|   a = builtins.trace "before inner break" (
            3|     let meow' = 3; in builtins.break { msg = "hello"; }
             |                       ^
            4|   );

       error: breakpoint reached
```
