---
synopsis: Deprecated language features
issues: [fj#437]
cls: [1785, 1736, 1735, 1744]
category: Breaking Changes
credits: [piegames, horrors]
---

A system for deprecation (and then the planned removal) of undesired language features has been put into place.
It is controlled via feature flags much like experimental features, except that the deprecations are enabled default,
and can be disabled via the flags for backwards compatibility (opt-out with `--extra-deprecated-features` or the Nix configuration file).

- `url-literals`: **URL literals** have long been obsolete and discouraged of use, and now they are officially deprecated.
  This means that all URLs must be properly put within quotes like all other strings.
- `rec-set-overrides`: **__overrides** is an old arcane syntax which has not been in use for more than a decade.
  It is soft-deprecated with a warning only, with the plan to turn that into an error in a future release.
