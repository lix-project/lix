---
name: broken-string-escape
internalName: BrokenStringEscape
timeline:
  - date: 2024-12-12
    release: 2.95.0
    cls: [2310]
    message: Introduced as a warning.
---
In Nix, string literals define syntax for escaping special characters like `\n`.
Only a limited set of escape rules are defined.
All characters without defined escape sequence escape "as themselves", e.g. `"\f"` becomes simply `f` instead of a form feed character.
Using these fallback escape sequences is now deprecated, because all usage sites in the wild found so far have been proven to be erroneous,
where the string did not end up the way the author likely intended.
For example when writing a regex in Nix, `"\."` will evaluate to the pattern `.` instead of the probably intended `\.` for matching a literal dot character, for which `"\\."` would have been correct instead.

To fix this, carefully evaluate each usage site for its intended usage and either remove the backslash or add a second backslash depending on the context.
