---
synopsis: Interrupting builds in the REPL works more than once
cls: 1097
---

Builds in the REPL can be interrupted by pressing Ctrl+C.
Previously, this only worked once per REPL session; further attempts would be ignored.
This issue is now fixed, so that builds can be canceled consistently.
