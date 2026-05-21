---
args: ['--repl-overlays', '{PWD}/repl-overlay-fail.nix']
should_fail: True
files: ['repl-overlay-fail.nix']
---

`repl-overlays` that fail to evaluate should error.

```output
Lix VERSION
Type :? for help.
Loading 'repl-overlays'...
error:
       … while calling anonymous lambda
         at «string»:2:16:
            1|
            2| info: initial: functions:
             |                ^
            3| let

       … while evaluating final
         at «string»:6:1:
            5| in
            6| final
             | ^
            7|

       … while calling the 'foldl'' builtin
         at «string»:4:11:
            3| let
            4|   final = builtins.foldl' (prev: function: prev // (function info final prev)) initial functions;
             |           ^
            5| in

       … while calling anonymous lambda
         at «string»:4:34:
            3| let
            4|   final = builtins.foldl' (prev: function: prev // (function info final prev)) initial functions;
             |                                  ^
            5| in

       … from call site
         at «string»:4:53:
            3| let
            4|   final = builtins.foldl' (prev: function: prev // (function info final prev)) initial functions;
             |                                                     ^
            5| in

       … while calling anonymous lambda
         at /pwd/repl-overlay-fail.nix:1:14:
            1| info: final: prev: builtins.abort "uh oh!"
             |              ^
            2|

       … while calling the 'abort' builtin
         at /pwd/repl-overlay-fail.nix:1:20:
            1| info: final: prev: builtins.abort "uh oh!"
             |                    ^
            2|

       error: evaluation aborted with the following error message: 'uh oh!'
```
