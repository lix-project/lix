---
name: 'false'
type: boolean
constructorArgs: ['false']
renameInGlobalScope: false
---
Primitive value.

It can be returned by
[comparison operators](@docroot@/language/operators.md#Comparison)
and used in
[conditional expressions](@docroot@/language/constructs.md#Conditionals).

The name `false` is not special, and can be shadowed:

```nix-repl
nix-repl> let false = 1; in false
1
```
