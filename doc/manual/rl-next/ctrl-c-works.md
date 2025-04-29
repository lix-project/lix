---
synopsis: "Ctrl-C works correctly on macOS again"
cls: [3066]
issues: [fj#729]
category: Fixes
credits: [jade]
---

Due to a kernel bug in macOS's `poll(2)` implementation where it would forget about event subscriptions, our detection of closed connections in the Lix daemon didn't work and left around lingering daemon processes.
We have rewritten that thread to use `kqueue(2)`, which is what the `poll(2)` implementation uses internally in the macOS kernel, so now Ctrl-C on clients will reliably terminate daemons once more.

This FD close monitoring has had the highest Apple bug ID references per line of code anywhere in the project, and hopefully not using poll anymore will stop us hitting bugs in poll.
