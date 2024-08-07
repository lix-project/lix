---
synopsis: "Better usage of colour control environment variables"
cls: [1699, 1702]
credits: [jade]
category: Improvements
---

Lix now heeds `NO_COLOR`/`NOCOLOR` for more output types, such as that used in `nix search`, `nix flake metadata` and similar.

It also now supports `CLICOLOR_FORCE`/`FORCE_COLOR` to force colours regardless of whether there is a terminal on the other side.

It now follows rules compatible with those described on <https://bixense.com/clicolors/> with `CLICOLOR` defaulted to enabled.

That is to say, the following procedure is followed in order:
- NO_COLOR or NOCOLOR set

  Always disable colour
- CLICOLOR_FORCE or FORCE_COLOR set

  Enable colour
- The output is a tty; TERM != "dumb"

  Enable colour
- Otherwise

  Disable colour
