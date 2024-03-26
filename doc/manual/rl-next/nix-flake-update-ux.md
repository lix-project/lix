---
synopsis: "`Overhaul `nix flake update` and `nix flake lock` UX"
prs: 8817
---

The interface for creating and updating lock files has been overhauled:

- [`nix flake lock`](@docroot@/command-ref/new-cli/nix3-flake-lock.md) only creates lock files and adds missing inputs now.
It will *never* update existing inputs.

- [`nix flake update`](@docroot@/command-ref/new-cli/nix3-flake-update.md) does the same, but *will* update inputs.
- Passing no arguments will update all inputs of the current flake, just like it already did.
- Passing input names as arguments will ensure only those are updated. This replaces the functionality of `nix flake lock --update-input`
- To operate on a flake outside the current directory, you must now pass `--flake path/to/flake`.

- The flake-specific flags `--recreate-lock-file` and `--update-input` have been removed from all commands operating on installables.
They are superceded by `nix flake update`.
