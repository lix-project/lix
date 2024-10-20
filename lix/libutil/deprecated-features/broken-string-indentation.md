---
name: broken-string-indentation
internalName: BrokenStringIndent
timeline:
  - date: 2026-01-30
    release: 2.95.0
    cls: [2092]
    message: Introduced as a warning.
---
Allow indented strings (those starting with  `''`) even when the indentation stripping will produce incorrect and probably unintended results.
Affected strings are:

- Single line indented strings that start with whitespace: `''    foo''` will be stripped of its leading space to `foo`.
  To fix this, convert the string to `"` or manually concatenate in the leading whitespace.
- Multi line indented strings with text on the first line:
  ```
  ''foo
    bar
  ''

  ```
  Having text on the first line here will completely disable the indentation stripping, which is unlikely desired.
  To fix this, move the contents of the first line down by one line.
