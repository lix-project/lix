---
name: unsafeGetAttrPos
args: [s, attrset]
---
`unsafeGetAttrPos` returns the position of the original definition (possibly
before operations like set merges) of the attribute named *s* from *attrset*.

If the position couldn't be determined (for example the set was produced by
builtin functions like `mapAttrs`), `null` will be returned instead, otherwise
a attrset will be returned, and it has the following attributes:

- `file` (string)
- `line` (number)
- `column` (number)

This is unsafe because it allows us to distinguish sets that compare equal but
are defined at different locations.
