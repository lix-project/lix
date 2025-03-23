---
synopsis: "Flake inputs/`builtins.fetchTree` invocations with `type = \"file\"` now have consistent (but different from previous versions) resulting paths"
issues: [fj#750]
cls: [2864]
category: "Breaking Changes"
credits: [jade]
---

Previously `fetchTree { type = "file"; url = "...", narHash = "sha256-..."; }` could return a different result depending on whether someone has run `nix store add-path --name source ...` on a path with the same `narHash` as the flake input/`fetchTree` invocation (or if such a path exists in an accessible binary cache).

In the past `type = "file"` flake inputs were, in contrast to all other flake inputs, hashed in *flat* hash mode rather than *recursive* hash mode.
The difference between the two is that *flat* mode hashes are just what you get from `sha256sum` of a single file, whereas *recursive* hashes are the SHA256 sum of a NAR (Nix ARchive, a deterministic tarball-like format) of a file tree.

Much of flakes assumes that everything is recursive-hashed including `nix flake archive`, substitution of flake inputs from binary caches, and more, which led to the substitution path code being taken if such a path is present, yielding a different store path non-deterministically.

To fix this non-deterministic evaluation bug, we needed to break derivation hash stability, so some Nix evaluations now produce different results than previous versions of Lix.
Lix now has consistent behaviour with CppNix 2.24 with respect to `file` flake inputs: they are *always* recursively hashed.
