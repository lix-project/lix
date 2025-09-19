---
synopsis: "Fix nix develop for derivations that rejects dependencies with structured attrs"
issues: [fj#997]
cls: [4182]
category: Fixes
credits: [raito]
---

For the sake of concision, we refer to `disallowedReferences` in what follows,
but all output checks were equally fixed:
`{dis,}allowed{References,Requisites}`.

Derivations can define *output checks* to reject unwanted dependencies, such as
interpreters like `bash` or compilers like `gcc`. This can be done in two ways:

* **Legacy style**: `disallowedReferences = [ ... ]` in the environment.
* **Structured attrs**: `outputChecks.<output>.disallowedReferences = [ ... ]`,
  typically used in `__json`.

Only the structured form supports derivations with multiple outputs.

`nix develop` internally rewrites derivations to create development shells. It
relied on the legacy `disallowedReferences`, and failed to honor the structured
variant. This led to broken shells in cases where `bashInteractive` was
explicitly disallowed using structured output checks, e.g. `nix develop
nixpkgs#systemd` after the "bash-less NixOS" changes.

This fix teaches `nix develop` to respect structured output checks, restoring
support for such derivations.
