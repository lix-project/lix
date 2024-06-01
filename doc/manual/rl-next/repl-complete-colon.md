---
synopsis: "`nix repl` now allows tab-completing the special repl :colon commands"
cls: 1367
credits: Qyriad
category: Improvements
---

The REPL (`nix repl`) supports pressing `<TAB>` to complete a partial expression, but now also supports completing the special :colon commands as well (`:b`, `:edit`, `:doc`, etc), if the line starts with a colon.
