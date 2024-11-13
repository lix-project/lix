---
synopsis: Small error message improvements
issues: []
cls: [2185]
category: Improvements
credits: [piegames]
---

Failed asserts don't print the failed assertion expression anymore in the error message. That code was buggy and the information was redundant anyways, given that the error position already more accurately shows what exactly failed.
