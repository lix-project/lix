---
name: addDrvOutputDependencies
args: [s]
---
Create a copy of the given string where a single constant string context element is turned into a "derivation deep" string context element.

The store path that is the constant string context element should point to a valid derivation, and end in `.drv`.

The original string context element must not be empty or have multiple elements, and it must not have any other type of element other than a constant or derivation deep element.
The latter is supported so this function is idempotent.

This is the opposite of [`builtins.unsafeDiscardOutputDependency`](#builtins-unsafeDiscardOutputDependency).
