---
name: addErrorContext
args: [message, expr]
---

This adds a `message` to be shown in the stacktrace in the event of
a failure during the evaluation of `expr`.

For example, if a file `err.nix` contains the following:

```nix
let
  countDown =
    n:
    if n == 0 then
      throw "kaboom"
    else
      builtins.addErrorContext "while counting down; n = ${toString n}" ("x" + countDown (n - 1));
in
countDown 2
```

Then, evaluating the file will give the following stack trace:

```console
$ nix-instantiate --show-trace err.nix
error:
       … from call site
         at /home/plop/git.lix.systems/lix-project/lix/err.nix:9:1:
            8| in
            9| countDown 2
             | ^
           10|

       … while calling 'countDown'
         at /home/plop/git.lix.systems/lix-project/lix/err.nix:3:5:
            2|   countDown =
            3|     n:
             |     ^
            4|     if n == 0 then

       … while calling the 'addErrorContext' builtin
         at /home/plop/git.lix.systems/lix-project/lix/err.nix:7:7:
            6|     else
            7|       builtins.addErrorContext "while counting down; n = ${toString n}" ("x" + countDown (n - 1));
             |       ^
            8| in

       … while counting down; n = 2

       … from call site
         at /home/plop/git.lix.systems/lix-project/lix/err.nix:7:80:
            6|     else
            7|       builtins.addErrorContext "while counting down; n = ${toString n}" ("x" + countDown (n - 1));
             |                                                                                ^
            8| in

       … while calling 'countDown'
         at /home/plop/git.lix.systems/lix-project/lix/err.nix:3:5:
            2|   countDown =
            3|     n:
             |     ^
            4|     if n == 0 then

       … while calling the 'addErrorContext' builtin
         at /home/plop/git.lix.systems/lix-project/lix/err.nix:7:7:
            6|     else
            7|       builtins.addErrorContext "while counting down; n = ${toString n}" ("x" + countDown (n - 1));
             |       ^
            8| in

       … while counting down; n = 1

       … from call site
         at /home/plop/git.lix.systems/lix-project/lix/err.nix:7:80:
            6|     else
            7|       builtins.addErrorContext "while counting down; n = ${toString n}" ("x" + countDown (n - 1));
             |                                                                                ^
            8| in

       … while calling 'countDown'
         at /home/plop/git.lix.systems/lix-project/lix/err.nix:3:5:
            2|   countDown =
            3|     n:
             |     ^
            4|     if n == 0 then

       … caused by explicit throw
         at /home/plop/git.lix.systems/lix-project/lix/err.nix:5:7:
            4|     if n == 0 then
            5|       throw "kaboom"
             |       ^
            6|     else

       error: kaboom
```
