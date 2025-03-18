---
synopsis: "`builtins.fetchTree` is no longer visible in `builtins` when flakes are disabled"
cls: [2399]
category: Fixes
credits: jade
---
`builtins.fetchTree` is the foundation of flake inputs and flake lock files, but is not fully specified in behaviour, which leads to regressions, behaviour differences with CppNix, and other unfun times.
It's gated behind the `flakes` experimental feature, but prior to now, would throw an uncatchable error at runtime when used without the `flakes` feature enabled.
Now it's like other builtins which are experimental feature gated, where it is not visible without the relevant feature enabled.

This fixes a bug in using Eelco Dolstra's version of flake-compat on Lix (and a divergence with CppNix): https://github.com/edolstra/flake-compat/issues/66
