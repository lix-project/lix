---
synopsis: Fix interference of the multiline progress bar with output
category: Fixes
cls: [2774]
credits: [alois31]
---
In some situations, the progress indicator of the multiline progress bar would interfere with persistent output.
This would result in progress bar headers being visible in place of the desired text, for example the outputs shown after a `:b` command in the repl.
The underlying ordering issue has been fixed, so that the undesired interference does not happen any more.
