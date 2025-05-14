---
synopsis: Fix handling of OSC codes in terminal output
issues: [fj#160]
cls: [3143]
category: Fixes
credits: [lilyball]
---

OSC codes in terminal output are now handled correctly, where OSC 8 (hyperlink) is preserved any
time color codes are allowed and all other OSC codes are stripped out. This applies not only to
output from build commands but also to rendered documentation in the REPL.
