---
name: tryEval
args: [e]
---
Try to shallowly evaluate *e*. Return a set containing the
attributes `success` (`true` if *e* evaluated successfully,
`false` if an error was thrown) and `value`, equalling *e* if
successful and `false` otherwise. `tryEval` will only prevent
errors created by `throw` or `assert` from being thrown.
Errors `tryEval` will not catch are for example those created
by `abort` and type errors generated by builtins. Also note that
this doesn't evaluate *e* deeply, so `let e = { x = throw ""; };
in (builtins.tryEval e).success` will be `true`. Using
`builtins.deepSeq` one can get the expected result:
`let e = { x = throw ""; }; in
(builtins.tryEval (builtins.deepSeq e e)).success` will be
`false`.