---
synopsis: 'Flakes follow `--eval-system` where it makes sense'
issues: [fj#673, fj#692, gh#11359]
cls: [2657]
category: Fixes
credits: [jade]
---

Most flake commands now follow `--eval-system` when choosing attributes to build/evaluate/etc.

The exceptions are commands that actually run something on the local machine:
- nix develop
- nix run
- nix upgrade-nix
- nix fmt
- nix bundle

This is not a principled approach to cross compilation or anything, flakes still impede rather than support cross compilation, but this unbreaks many remote build use cases.
