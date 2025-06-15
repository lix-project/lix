---
synopsis: First argument to `--arg`/`--argstr` must be a valid Nix identifier
issues: [fj#496]
category: "Breaking Changes"
credits: [ma27]
---

The first argument to `--arg`/`--argstr` must be a valid Nix identifier, i.e.
`nix-build --arg config.allowUnfree true` is now rejected.

This is because that invocation is a false friend since it doesn't set
`{ config = { allowUnfree = true; }; }`, but `{ "config.allowUnfree" = true; }`.

The idea is to change the behavior to the latter in the long-term. For that,
non-identifiers started giving a warning since 2.92 and are now rejected to give people
who depend on that a chance to notice and potentially weigh in on the discussion.
