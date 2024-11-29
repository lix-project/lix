---
synopsis: Deprecated language features
issues: [fj#437, 861]
cls: [1785, 1736, 1735, 1744, 2206]
category: Breaking Changes
credits: [piegames, horrors]
---

A system for deprecation (and then the planned removal) of undesired language features has been put into place.
It is controlled via feature flags much like experimental features, except that the deprecations are enabled default,
and can be disabled via the flags for backwards compatibility (opt-out with `--extra-deprecated-features` or the Nix configuration file).

- `url-literals`: **URL literals** have long been obsolete and discouraged of use, and now they are officially deprecated.
  This means that all URLs must be properly put within quotes like all other strings.
- `rec-set-overrides`: **__overrides** is an old arcane syntax which has not been in use for more than a decade.
  It is soft-deprecated with a warning only, with the plan to turn that into an error in a future release.
- `ancient-let`: **The old `let` syntax** (`let { body = …; … }`) is soft-deprecated with a warning as well. Use the regular `let … in` instead.
- `shadow-internal-symbols`: Arithmetic expressions like `5 - 3` internally expand to `__sub 5 3`, where `__sub` maps to a subtraction builtin. Shadowing such a symbols would affect the evaluation of such operations, but in a very inconsistent way, and is therefore deprecated now. **Affected symbols are:** `__sub`, `__mul`, `__div`, `__lessThan`, `__findFile` and `__nixPath`. Note that these symbols may still be used as variable names as long as they do not shadow internal operations, so e.g. `let __sub = x: y: x + y; in __sub 3 5` remains valid code.
