---
synopsis: "Function equality semantics are more consistent, but still bad"
cls: [4556, 4244]
issues: []
category: "Breaking Changes"
credits: [horrors]
---

Lix has inherited a historic misfeature from CppNix in the form of pointer
equality checks built into the `==` operator. These checks were originally
meant to optimize comparison for large sets, but they have the unfortunate
side effect of producing unexpected results when sets containing functions
are compared. **Lix 2.93 and earlier** behave as shown in the repl session

```
Lix 2.93.3
Type :? for help.
nix-repl> f = x: x
Added f.

nix-repl> f == f
false

nix-repl> let s.f = f; in s.f == s.f
false

nix-repl> # however!
          { inherit f; } == { inherit f; }
true

nix-repl> [ f ] == [ f ]
true

nix-repl> # and, in another twist:
          [ f ] == map f [ f ]
false
```

Nixpkgs relies on sets containing functions being comparable, so we cannot
simply deprecate this behavior. Due to changes to the object model used by
Lix ***all* comparisons above now evaluate to `true`**. This is considered
a breaking change because eval results may differ, but we also consider it
minor because the optimization is unsound (c.f. `let l = [NaN]; in l == l`
evaluates to `true` even though floating point `NaN` is incomparable). Lix
intends to remove this optimization altogether in the future, but until we
can do that we instead make it slightly less broken to allow other, *real*
optimizations. Function equality comparison remains **undefined behavior**
and should not be relied upon in Nixlang code that intends to be portable.
