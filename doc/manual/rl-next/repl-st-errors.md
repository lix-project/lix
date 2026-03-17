---
synopsis: "invalid arguments to :st now print an error"
cls: [5386]
category: "Improvements"
credits: [blokyk]
---

When using the debugger, the `:st` command used to traverse the call stack would
silently fail and put the debugger in an invalid state if the argument given to
it wasn't a valid stack frame index.

This change adds an error message warning the user if the given index wasn't a
valid frame (telling them the range of valid indices), as well as if it wasn't
even a valid integer to begin with.
