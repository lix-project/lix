---
synopsis: "REPL now uses rustyline"
cls: [5703]
category: "Improvements"
credits: [horrors]
issues: []
---

The REPL now uses [rustyline](https://github.com/kkawakam/rustyline) for input processing instead
of editline. This comes with some improvements to REPL behavior: wrapping lines no longer confuse
the line editor, unicode is fully supported, pasting multiline expressions is noew possible, even
undo commands are now available! We plan to improve the REPL further using these newfound powers.
