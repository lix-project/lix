---
name: toString
args: [e]
renameInGlobalScope: false
---
Convert the expression *e* to a string. *e* can be:

  - A string (in which case the string is returned unmodified).

  - A path (e.g., `toString /foo/bar` yields `"/foo/bar"`.

  - A set containing `{ __toString = self: ...; }` or `{ outPath = ...; }`.

  - An integer.

  - A floating-point value, it will be converted to the decimal notation in the style `[-]ddd.ddd` with 6 digits appearing after the decimal point.

  - A list, in which case the string representations of its elements
    are joined with spaces.

  - A Boolean (`false` yields `""`, `true` yields `"1"`).

  - `null`, which yields the empty string.
