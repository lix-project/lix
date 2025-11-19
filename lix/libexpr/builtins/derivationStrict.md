---
name: derivationStrict
args: [args]
renameInGlobalScope: false
---

Constructs a [store derivation](../glossary.md#gloss-store-derivation) from the attribute set `args`
(c.f. [derivation](#builtins-derivation). Unlike `derivation` the produced store derivation is placed
in the store *immediately* when this builtin is called, while `derivation` may defer placing store
derivations in the store until it is proven that they are used.

It then returns a new attrset with *only* the following attributes:

- `drvPath` containing the path of the store derivation;
- For each output of the derivation (`out`, `dev`, etc): an attribute named after that output containing the output path

> **Note**
>
> In contrast to [`builtins.derivation`](#builtins-derivation), this computes
> the derivation set in a fully *strict* manner, i.e. the values of the attributes
> directly computed, whereas using `builtins.derivation` will produce an attrset
> whose values will be evaluated when they are used at a later point.
