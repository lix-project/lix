---
name: dynamic-derivations
internalName: DynamicDerivations
---
Allow the use of a few things related to dynamic derivations:

  - "text hashing" derivation outputs, so we can build .drv
    files.

  - dependencies in derivations on the outputs of
    derivations that are themselves derivations outputs.
