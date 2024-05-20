---
synopsis: "Add an option `--unpack` to unpack archives in `nix store prefetch-file`"
prs: 9805
cls: 224
credits: [yshui, horrors]
category: Improvements
---

It is now possible to fetch an archive then NAR-hash it (as in, hash it in the
same manner as `builtins.fetchTarball` or fixed-output derivations with
recursive hash type) in one command.

Example:

```
~ » nix store prefetch-file --name source --unpack https://git.lix.systems/lix-project/lix/archive/2.90-beta.1.tar.gz
Downloaded 'https://git.lix.systems/lix-project/lix/archive/2.90-beta.1.tar.gz' to '/nix/store/yvfqnq52ryjc3janw02ziv7kr6gd0cs1-source' (hash 'sha256-REWlo2RYHfJkxnmZTEJu3Cd/2VM+wjjpPy7Xi4BdDTQ=').
```
