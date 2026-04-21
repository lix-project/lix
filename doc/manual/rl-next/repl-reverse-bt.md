---
synopsis: "Print REPL backtraces in more convenient order"
issues: []
cls: [5491]
category: "Improvements"
credits: [blokyk]
---

When using the debugger, stack traces printed with the `:bt` command were
previously printed in reverse order compared to most other situations where they
appeared: the current stack frame would be printed at the very top, with the
most outer frame at the bottom, meaning that you'd have to scroll up to get a
sense of where you are.

With this change, the stack frames are printed such that the most relevant ones
are immediatly visible at the bottom, just like other traces in lix (e.g.
ones caused by errors).
