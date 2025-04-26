---
synopsis: 'Implicit `__toString` now have stack trace entries'
issues: []
cls: [3055]
category: Improvements
credits: [horrors]
---

Coercion of attribute sets to strings via their `__toString` attribute now produce stack
frames pointing to the coercion site and the attribute definition. This makes locating a
coercion function error easier as the fault location is now more likely to be presented.

Previously:
```
nix-repl> builtins.substring 1 1 "${{ __toString = self: throw ''bar''; }}"
error:
       … while calling the 'substring' builtin
         at «string»:1:1:
            1| builtins.substring 1 1 "${{ __toString = self: throw ''bar''; }}"
             | ^

       … caused by explicit throw
         at «string»:1:48:
            1| builtins.substring 1 1 "${{ __toString = self: throw ''bar''; }}"
             |                                                ^

       error: bar
```

Now:
```
nix-repl> builtins.substring 1 1 "${{ __toString = self: throw ''bar''; }}"
error:
       … while calling the 'substring' builtin
         at «string»:1:1:
            1| builtins.substring 1 1 "${{ __toString = self: throw ''bar''; }}"
             | ^

       … while converting a set to string
         at «string»:1:25:
            1| builtins.substring 1 1 "${{ __toString = self: throw ''bar''; }}"
             |                         ^

       … from call site
         at «string»:1:29:
            1| builtins.substring 1 1 "${{ __toString = self: throw ''bar''; }}"
             |                             ^

       … while calling '__toString'
         at «string»:1:42:
            1| builtins.substring 1 1 "${{ __toString = self: throw ''bar''; }}"
             |                                          ^

       … caused by explicit throw
         at «string»:1:48:
            1| builtins.substring 1 1 "${{ __toString = self: throw ''bar''; }}"
             |                                                ^

       error: bar
```
