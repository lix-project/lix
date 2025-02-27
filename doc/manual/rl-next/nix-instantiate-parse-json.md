---
synopsis: '`nix-instantiate --parse` outputs json'
issues: [fj#487, nix#11124, nix#4726, nix#3077]
cls: [2190]
category: Breaking Changes
credits: [piegames, horrors]
---

`nix-instantiate --parse` does not print out the AST in a Nix-like format anymore.
Instead, it now prints a JSON representation of the internal expression tree.
Tooling should not rely on the stdout of `nix-instantiate --parse`.

We've done our best to ensure that the new behavior is as compatible with the old one as possible.
If you depend on the old behavior in ways that are not covered anymore or are otherwise negatively affected by this change,
then please reach out so that we can find a sustainable solution together.
