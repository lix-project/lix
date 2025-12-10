---
name: or-as-identifier
internalName: OrAsIdentifier
timeline:
  - date: 2026-01-30
    release: 2.95.0
    cls: [4764]
    message: Introduced as a warning.
---
Back when the `or` operator was introduced, instead of making it a proper keyword, the syntax was adapted in attempt of making it a context-sensitive keyword without disrupting existing code.
This attempt has backfired, because it causes glitches in the operator precedence when a variable is called `or`:
`let or = 1; in [ (x: x) or ]` evaluates to a list with one element, but `let nor = 1; in [ (x: x) nor ]` evaluates to a list with two elements.
These old backwards compatibility hacks are deprecated in favor of treating `or` as a full language keyword.

To fix this, rename affected attributes or put the attribute name in quotes (`"or"`).
