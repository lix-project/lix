---
synopsis: Small error message improvements
issues: []
cls: [2185, 2187]
category: Improvements
credits: [piegames]
---

When an attribute selection fails, the error message now correctly points to the attribute in the chain that failed instead of at the beginning of the entire chain.
```diff
 error: attribute 'x' missing
-       at /pwd/lang/eval-fail-remove.nix:4:3:
+       at /pwd/lang/eval-fail-remove.nix:4:29:
             3| in
             4|   (removeAttrs attrs ["x"]).x
-             |   ^
+             |                             ^
             5|
```

Failed asserts don't print the failed assertion expression anymore in the error message. That code was buggy and the information was redundant anyways, given that the error position already more accurately shows what exactly failed.
