---
synopsis: Ctrl-C stops Nix commands much more reliably and responsively
issues: [7245, fj#393]
cls: [2016]
prs: [11618]
category: Fixes
credits: [roberth, 9999years]
---

CTRL-C will now stop Nix commands much more reliably and responsively. While
there are still some cases where a Nix command can be slow or unresponsive
following a `SIGINT` (please report these as issues!), the vast majority of
signals will now cause the Nix command to quit quickly and consistently.
