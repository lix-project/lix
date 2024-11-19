---
name: getContext
args: [s]
---
Return the string context of *s*.

The string context tracks references to derivations within a string.
It is represented as an attribute set of [store derivation](@docroot@/glossary.md#gloss-store-derivation) paths mapping to output names.

Using [string interpolation](@docroot@/language/string-interpolation.md) on a derivation will add that derivation to the string context.
For example,

```nix
builtins.getContext "${derivation { name = "a"; builder = "b"; system = "c"; }}"
```

evaluates to

```
{ "/nix/store/arhvjaf6zmlyn8vh8fgn55rpwnxq0n7l-a.drv" = { outputs = [ "out" ]; }; }
```
