---
synopsis: Increase default stack size on macOS
prs: 9860
credits: 9999years
category: Improvements
---

Increase the default stack size on macOS to the same value as on Linux, subject to system restrictions to maximum stack size.
This should reduce the number of stack overflow crashes on macOS when evaluating Nix code with deep call stacks.
