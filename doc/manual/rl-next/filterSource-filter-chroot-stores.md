---
synopsis: fix usage of `builtins.filterSource` and `builtins.path` with the filter argument when using chroot stores
issues: [nix#11503]
credits: [lily, alois31, horrors]
category: Fixes
---

The semantics of `builtins.filterSource` (and the `filter` argument for
`builtins.path`) have been adjusted regarding how paths inside the Nix store
are handled.

Previously, when evaluating whether a path should be included, the filtering
function received the **physical path** if the source was inside the chroot store.

Now, it receives the **logical path** instead.

This ensures consistency in path handling and avoids potential
misinterpretations of paths within the evaluator, which led to various fallouts
mentioned in <https://github.com/NixOS/nixpkgs/pull/369694>.
