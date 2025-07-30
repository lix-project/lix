---
synopsis: Derivations can now be printed in detail in `nix repl`
cls: [3842]
category: Improvements
credits: [Lunaphied]
---

Traditionally derivations printed in the REPL would only print a formatted object
representing the path of the derivation file it refers to. This makes inspecting
the enhanced derivation attribute sets encountered from `mkDerivation` or similar
wrappers more difficult. Even the `:p`/`:print` command would not elaborate attribute sets
tagged as a derivation.

With this change you can now use `:p`/`:print` to directly inspect a derivation
by providing one as the top-level object. Derivation attribute sets will only be
printed two levels deep and internal derivation attrsets will remain in unexpanded
path form as before. `drvAttrs` will also be elided as these attributes are already
present in the top-level attribute set of the derivation. These heuristics provide
a balance between readability and functionality. When the `:p`/`:print` is omitted,
a bare derivation is printed in the path format as before.
