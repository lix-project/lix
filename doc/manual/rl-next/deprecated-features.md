---
synopsis: 'more deprecated features'
issues: []
cls: [2092, 2310, 2311, 4638, 4652]
category: Breaking Changes
credits: [piegames, commentator2.0]
---
This release cycle features a new batch of deprecated (anti-)features.
You can opt in into the old behavior with `--extra-deprecated-features` or any equivalent configuration option.

- `broken-string-indentation` indented strings (those starting with  `''`) might produce unintended results due to how the whitespace stripping is done. Those cases will now warn the user.
- `broken-string-escape` "escaped" characters without a properly defined escape sequence evaluate to "themselves". This is in most cases unintended behaviour, both for writing regexes, and using legacy or uncommon escape sequences like `\f`. The user will now be warned, if those are present.
- `floating-without-zero` so far, one was able to declare a float using something like `.123`. This can cause confusion about accessing attributes. Floating point numbers must now always include the leading zero, i.e. `0.123`
- `rec-set-merges` Attribute sets like `{ foo = {}; foo.bar = 42;}` implicitly merge at parse time, however if one of them is marked as recursive but not the others then the recursive attribute may get lost (order-dependent). Therefore, merging attrs with mixed-`rec` is now forbidden.
- `rec-set-dynamic-attrs` Dynamic attributes have weird semantics in the presence of recursive attrsets (they evaluate *after* the rest of the set). This is now forbidden.
