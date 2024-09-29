---
synopsis: restore backwards-compatibility of `builtins.fetchGit` with Nix 2.3
issues: [5291, 5128]
credits: [ma27]
category: Fixes
---

Compatibility with `builtins.fetchGit` from Nix 2.3 has been restored as follows:

* Until now, each `ref` was prefixed with `refs/heads` unless it starts with `refs/` itself.

  Now, this is not done if the `ref` looks like a commit hash.

* Specifying `builtins.fetchGit { ref = "a-tag"; /* … */ }` was broken because `refs/heads` was appended.

  Now, the fetcher doesn't turn a ref into `refs/heads/ref`, but into `refs/*/ref`. That way,
  the value in `ref` can be either a tag or a branch.

* The ref resolution happens the same way as in git:

  * If `refs/ref` exists, it's used.
  * If a tag `refs/tags/ref` exists, it's used.
  * If a branch `refs/heads/ref` exists, it's used.
