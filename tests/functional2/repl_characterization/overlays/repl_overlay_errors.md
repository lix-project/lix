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
         at «string»:1:16:
            1| info: initial: functions:
             |                ^
            2| let

       … while evaluating final
         at «string»:5:1:
            4| in
            5| final
             | ^
            6|

       … while calling the 'foldl'' builtin
         at «string»:3:11:
            2| let
            3|   final = builtins.foldl' (prev: function: prev // (function info final prev)) initial functions;
             |           ^
            4| in

       … while calling anonymous lambda
         at «string»:3:34:
            2| let
            3|   final = builtins.foldl' (prev: function: prev // (function info final prev)) initial functions;
             |                                  ^
            4| in

       … from call site
         at «string»:3:53:
            2| let
            3|   final = builtins.foldl' (prev: function: prev // (function info final prev)) initial functions;
             |                                                     ^
            4| in

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
