---
synopsis: "`nix-shell` default shell directory is not `/tmp` anymore for `$NIX_BUILD_TOP`"
cls: []
issues: [fj#940]
category: "Fixes"
credits: [raito]
---

Previously, Lix `nix-shell`s could exit non-zero status when `stdenv`'s `dumpVars` phase failed to write to `$NIX_BUILD_TOP/env-vars`, despite `dumpVars` being intended as a debugging aid.

This happens when `TMPDIR` is not set and defaults therefore to `/tmp`, resulting in a `/tmp/env-vars` global file that every `nix-shell` wants to write.

We fix this issue by reusing a pre-created, unique, and writable location, as the build top directory, avoiding shell exiting from write failures silently.
