---
synopsis: "Hitting Control-C twice always terminates Lix"
cls: [3574]
issues: []
category: "Improvements"
credits: [horrors]
---

Hitting Control-C or sending `SIGINT` to Lix now prints an informational message
if it is still running after on second, the second Control-C/`SIGINT` terminates
Lix immediately without waiting for any shutdown code to finish running. Lix did
not treat the second such event differently from first in the past; this made it
impossible to easily terminate running Lix processes that got stuck in e.g. very
expensive Nixlang code that never interacted with the store. We now terminate as
soon as the user hits Control-C again without waiting any more, to much the same
effect as putting Lix into the background and killing it immediately afterwards.

This means you can now more conveniently break out of stuck Nixlang evaluations:
```
❯ nix-instantiate --eval --expr 'let f = n: if n == 0 then 0 else f (n - 1) + f (n - 1); in f 32'
^CStill shutting down. Press ^C again to abort all operations immediately.
^C

❌130 ❯
```
