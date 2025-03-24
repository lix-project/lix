---
synopsis: "Add --raw flag to `nix-instantiate --eval` for unescaped output"
issues: []
prs: [gh#12119]
cls: [2886]
category: Improvements
credits: [not-my-profile, infinisil, raito]
---

The `nix-instantiate --eval` command now supports a `--raw` flag. When used,
the result must be coercible to a string (as with `${...}`) and is printed
verbatim, without quotes or escaping.
