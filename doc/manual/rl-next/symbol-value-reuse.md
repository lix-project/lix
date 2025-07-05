---
synopsis: Symbols reuses once-allocated Value to reduce garbage collected allocations
issues: []
cls: [3308, 3300, 3314, 3310, 3312, 3313]
category: Improvements
credits: [raito, horrors, thubrecht, xokdvium, nan-git]
---

In the Lix evaluator, **symbols** represent immutable strings, like those used
for attribute names.

In evaluator design, such strings are typically [**interned**](https://en.wikipedia.org/wiki/String_interning), stored uniquely
to save memory, and Lix inherits this approach from the original C++ codebase.

However, some builtins, like `builtins.attrNames`, must return a `Value` type
that can represent any Nix value (strings, integers, lists, etc.).

Before this change, these builtins would create lists of `Value` objects by
allocating them through the garbage collector, copying the symbol’s string
content each time.

This allocation is unnecessary if the interned symbols themselves also hold a
`Value` representation allocated outside the garbage collector, since these
live for the full duration of evaluation.

As a result, this reduces the number of allocations, leading to:

* A significant drop in maximum [resident set memory](https://en.wikipedia.org/wiki/Resident_set_size) (RSS), with some large-scale
  tests showing up to 11% (about 500 MiB) savings in large colmena deployments.
* A slight decrease in CPU usage during Nix evaluations.

This change is inspired by https://github.com/NixOS/nix/pull/13258 but the approach is different.

**Note** : [`xokdvium`](https://github.com/xokdvium) is the rightful author of https://gerrit.lix.systems/c/lix/+/3300 and the credit was missed on our end during the development process. We are deeply sorry for this mistake.
