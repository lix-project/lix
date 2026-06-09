---
args: ['--debugger', '-f', 'regression_9917.nix']
files: ['regression_9917.nix']
---

https://github.com/NixOS/nix/pull/9917 (Enter debugger more reliably in let expressions and function calls)

```output
Lix VERSION
Type :? for help.
trace: before outer break
info: breakpoint reached

Added 2 variables.
```

This test ensures that continues don't skip opportunities to enter the debugger.

```nix
:c
:bt
```

```output
trace: before inner break
info: breakpoint reached

Added 2 variables.

6: while evaluating the file '/pwd/regression_9917.nix':
/pwd/regression_9917.nix:1:1

     1| let
      | ^
     2|   a = builtins.trace "before inner break" (

5: while evaluating a 'let' expression
/pwd/regression_9917.nix:1:1

     1| let
      | ^
     2|   a = builtins.trace "before inner break" (

4: while calling a function
/pwd/regression_9917.nix:5:7

     4|   );
     5|   b = builtins.trace "before outer break" (
      |       ^
     6|     builtins.break a

3: while calling a function
/pwd/regression_9917.nix:6:5

     5|   b = builtins.trace "before outer break" (
     6|     builtins.break a
      |     ^
     7|   );

2: while calling a function
/pwd/regression_9917.nix:2:7

     1| let
     2|   a = builtins.trace "before inner break" (
      |       ^
     3|     builtins.break { msg = "hello"; }

1: while calling a function
/pwd/regression_9917.nix:3:5

     2|   a = builtins.trace "before inner break" (
     3|     builtins.break { msg = "hello"; }
      |     ^
     4|   );

0: error: breakpoint reached
/pwd/regression_9917.nix:3:5

     2|   a = builtins.trace "before inner break" (
     3|     builtins.break { msg = "hello"; }
      |     ^
     4|   );

```

```nix
:c
msg
```
```output
Loading installable ''...
Added 1 variables.
"hello"

```
