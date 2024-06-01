---
synopsis: Add log formats `multiline` and `multiline-with-logs`
cls: [1369]
credits: [kloenk]
category: Improvements
---

Added two new log formats (`multiline` and `multiline-with-logs`) that display
current activities below each other for better visibility.

These formats attempt to use the maximum available lines
(defaulting to 25 if unable to determine) and print up to that many lines.
The status bar is displayed as the first line, with each subsequent
activity on its own line.
