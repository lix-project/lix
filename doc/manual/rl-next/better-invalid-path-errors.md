---
synopsis: "Confusing 'invalid path' errors are now 'path does not exist'"
cls: [1161, 1160, 1159]
credits: midnightveil
category: Improvements
---

Previously, if a path did not exist in a Nix store, it was referred to as the internal name "path is invalid".
This is, however, very confusing, and there were numerous such errors that were exactly the same, making it hard to debug.
These errors are now more specific and refer to the path not existing in the store.
