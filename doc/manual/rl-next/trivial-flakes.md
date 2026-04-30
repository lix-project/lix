---
synopsis: "Changes to `flake.nix` validation"
cls: [5523]
category: "Breaking Changes"
credits: [piegames, Qyriad, horrors]
issues: [gh#4945]
---

Flakes try to keep their inputs and metadata "simple", to make sure no unbounded computation may happen when calling e.g. `nix flake show`.
Those checks were haphazard, a maintenance burden, and also easily circumventable.

Lix has now replaced all the old checks by a simple rule: **No function calls outside of `outputs`.**
This is easier to reason about than the previous set of inconsistent rules, and crucially now also allows syntax features that users felt like they *should* have worked in the past, like let bindings.
However, some warts still remain for now: Some syntax constructs like `-1` internally desugar to `__sub 0 1`, which is a function call and thus remains forbidden.
This will be rectified as soon as the deprecation period of the respective anti-features has been completed.

This change is **breaking** in the sense that flakes which are written with the newly allowed language features will not evaluate with an older Lix version which still uses the old, more restrictive checks.
Crucially, this also affects **all transitive dependants** of such Flakes.
