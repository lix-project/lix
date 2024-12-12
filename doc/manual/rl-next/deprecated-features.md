---
synopsis: 'more deprecated features'
issues: []
cls: [2092, 2310]
category: Breaking Changes
credits: [piegames, commentator2.0]
---
This release cycle features a new batch of deprecated (anti-)features.
You can opt in into the old behavior with `--extra-deprecated-features` or any equivalent configuration option.

- `broken-string-indentation` indented strings (those starting with  `''`) might produce unintended results due to how the whitespace stripping is done. Those cases will now warn the user.
- `broken-string-escape` "escaped" characters without a properly defined escape sequence evaluate to "themselves". This is in most cases unintended behaviour, both for writing regexes, and using legacy or uncommon escape sequences like `\f`. The user will now be warned, if those are present.
