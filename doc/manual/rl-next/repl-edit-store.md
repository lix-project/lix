---
synopsis: "`:edit`ing a file in Nix store no longer reloads the repl"
issues: [fj#341]
cls: [1620]
category: Improvements
credits: [goldstein]
---

Calling `:edit` from the repl now only reloads if the file being edited was outside of Nix store.
That means that all the local variables are now preserved across `:edit`s of store paths.
This is always safe because the store is read-only.
