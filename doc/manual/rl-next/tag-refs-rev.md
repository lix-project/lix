---
synopsis: "Consistently use commit hash as rev when locking git inputs"
cls: [4762]
category: "Fixes"
credits: [goldstein]
---

Lix will now use commit hashes instead of tag object hashes in the `rev` field
when fetching git inputs by tag in `flake.lock` and `builtins.fetchTree` output.
Note that this means that Lix may change some `flake.lock` files on re-locking. Old `flake.lock` files still remain valid.
