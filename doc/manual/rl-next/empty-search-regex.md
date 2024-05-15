---
synopsis: Disallow empty search regex in `nix search`
prs: 9481
credits: [iFreilicht, horrors]
category: Miscellany
---

[`nix search`](@docroot@/command-ref/new-cli/nix3-search.md) now requires a search regex to be passed. To show all packages, use `^`.
