---
synopsis: Add `inputs.self.submodules` flake attribute
issues: [fj#942]
cls: [3839]
category: Features
credits: [edolstra, kasimeka]
---

A port of <https://github.com/NixOS/nix/pull/12421> to Lix, which:

- adds a general `inputs.self` flake attribute that retroactively applies
  configurations to a flake after it's been fetched, then triggers a refetch of
  the flake with the new config.
- implements `inputs.self.submodules` that allows a flake to declare its need
  for submodules, which are then fetched automatically with no need to pass
  `?submodules=1` anywhere.
