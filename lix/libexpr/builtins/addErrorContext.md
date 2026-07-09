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
$ nix-instantiate err.nix
error:
       … while counting down; n = 2

       … while counting down; n = 1

       … caused by explicit throw
         at err.nix:5:7:
            4|     if n == 0 then
            5|       throw "kaboom"
             |       ^
            6|     else

       error: kaboom
```
