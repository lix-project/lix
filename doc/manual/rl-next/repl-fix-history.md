---
synopsis: "`nix repl` history is saved more reliably"
cls: 1164
credits: puck
---

`nix repl` now saves its history file after each line, rather than at the end
of the session; ensuring that it will remember what you typed even after it
crashes.
