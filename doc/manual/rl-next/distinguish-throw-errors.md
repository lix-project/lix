---
synopsis: "Distinguish between explicit throws and errors that happened while evaluating a throw"
cls: 1511
credits: Qyriad
category: Improvements
---

Previously, errors caused by an expression like `throw "invalid argument"` were treated like an error that happened simply while some builtin function was being called:

```
let
  throwMsg = p: throw "${p} isn't the right package";
in throwMsg "linuz"

error:
       … while calling the 'throw' builtin
         at «string»:2:17:
            1| let
            2|   throwMsg = p: throw "${p} isn't the right package";
             |                 ^
            3| in throwMsg "linuz"

       error: linuz isn't the right package
```

But the error didn't just happen "while" calling the `throw` builtin — it's a throw error!
Now it looks like this:

```
let
  throwMsg = p: throw "${p} isn't the right package";
in throwMsg "linuz"

error:
       … caused by explicit throw
         at «string»:2:17:
            1| let
            2|   throwMsg = p: throw "${p} isn't the right package";
             |                 ^
            3| in throwMsg "linuz"

       error: linuz isn't the right package
```

This also means that incorrect usage of `throw` or errors evaluating its arguments are easily distinguishable from explicit throws:

```
let
  throwMsg = p: throw "${p} isn't the right package";
in throwMsg { attrs = "error when coerced in string interpolation"; }

error:
       … while calling the 'throw' builtin
         at «string»:2:17:
            1| let
            2|   throwMsg = p: throw "${p} isn't the right package";
             |                 ^
            3| in throwMsg { attrs = "error when coerced in string interpolation"; }

       … while evaluating a path segment
         at «string»:2:24:
            1| let
            2|   throwMsg = p: throw "${p} isn't the right package";
             |                        ^
            3| in throwMsg { attrs = "error when coerced in string interpolation"; }

       error: cannot coerce a set to a string: { attrs = "error when coerced in string interpolation"; }
```

Here, instead of an actual thrown error, a type error happens first (trying to coerce an attribute set to a string), but that type error happened *while* calling `throw`.
