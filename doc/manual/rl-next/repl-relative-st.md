---
synopsis: "Allow moving between stack frames relative to current debugger frame"
issues: [1156]
cls: [5411]
category: "Improvements"
credits: [blokyk]
---

Debugging functional programs often involve switching between a bunch of stack
frames to get the full context of what's happening and who's calling who.
Before this change, going up or down the stack in the nix debugger with `:st`
meant remembering the absolute index of each stack frame, instead of their
positions relative to one another; this got tiring *fast*.

Now, you can prepend `:st`'s argument with a + or - sign to indicate you want to
move relative to the current stack frame. For example, typing `:st +3` when you
were on frame `10` will go frame `13`; vice-versa, typing `:st -4` on frame `6`
will go to frame `2`.
