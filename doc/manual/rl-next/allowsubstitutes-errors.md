---
synopsis: "Build failures caused by `allowSubstitutes = false` while being the wrong system now produce a decent error"
issues: [fj#484]
cls: [1841]
category: Fixes
credits: jade
---

Nix allows derivations to set `allowSubstitutes = false` in order to force them to be built locally without querying substituters for them.
This is useful for derivations that are very fast to build (especially if they produce large output).
However, this can shoot you in the foot if the derivation *has* to be substituted such as if the derivation is for another architecture, which is what `--always-allow-substitutes` is for.

Perhaps such derivations that are known to be impossible to build locally should ignore `allowSubstitutes` (irrespective of remote builders) in the future, but this at least reports the failure and solution directly.

```
$ nix build -f fail.nix
error: a 'unicornsandrainbows-linux' with features {} is required to build '/nix/store/...-meow.drv', but I am a 'x86_64-linux' with features {...}

       Hint: the failing derivation has allowSubstitutes set to false, forcing it to be built rather than substituted.
       Passing --always-allow-substitutes to force substitution may resolve this failure if the path is available in a substituter.
```
