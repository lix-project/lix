---
name: rec-set-merges
internalName: RecSetMerges
timeline:
  - date: 2026-01-30
    release: 2.95.0
    cls: [4638]
    message: Introduced as parser error.
---
Parsetime attribute set merging, as done e.g. by `{ foo.bar = 1; foo.baz = 2; }`, has confusing semantics when only one of the attribute sets is marked as recursive:
Whether the merged attrs is marked as recursive or not is dependent on the declaration order because of implementation quirks.
Because the merging behavior is relevant for the check for missing or duplicate attributes, this can have confusing second-order effects.
See <https://github.com/NixOS/nix/issues/9020> for more examples.
Because the parser cannot be fixed without introducing breaking changes to the language, a check has been introduced to forbid all code that would trigger the confusing behavior:
Two attributes can be merged only if both or neither of them are marked as recursive.

To fix this, manually perform the merge by moving the affected attribute declarations.
