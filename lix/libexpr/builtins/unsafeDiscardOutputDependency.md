---
name: unsafeDiscardOutputDependency
args: [s]
---
Create a copy of the given string where every "derivation deep" string context element is turned into a constant string context element.

This is the opposite of [`builtins.addDrvOutputDependencies`](#builtins-addDrvOutputDependencies).

This is unsafe because it allows us to "forget" store objects we would have otherwise referred to with the string context,
whereas Nix normally tracks all dependencies consistently.
Safe operations "grow" but never "shrink" string contexts.
[`builtins.addDrvOutputDependencies`] in contrast is safe because "derivation deep" string context element always refers to the underlying derivation (among many more things).
Replacing a constant string context element with a "derivation deep" element is a safe operation that just enlargens the string context without forgetting anything.

[`builtins.addDrvOutputDependencies`]: #builtins-addDrvOutputDependencies
