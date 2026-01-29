---
synopsis: 'more deprecated features'
issues: []
cls: [2092, 2310, 2311, 4638, 4652, 4764]
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
- `or-as-identifier` `or` as an identifier has always been weird since the `or` (almost-)keyword has been introduced. We are deprecating the backcompat hacks from the early days of Nix in favor of making `or` a full and proper keyword.
- `tokens-no-whitespace` Function applications without space around the arguments like `0a`, `0.00.0` or `foo"1"2` are now forbidden. The same applies to list elements. The primary reason for this deprecation is to remove foot guns around surprising tokenization rules regarding number literals, but this will also free up some syntax for other purposes (e.g. `r""` strings) for reuse at some point in the future.
- `shadow-internal-symbols` has been expanded to also forbid shadowing `null`, `true` and `false`.
- `ancient-let` deprecation has been turned into a full parser error instead of a warning.
