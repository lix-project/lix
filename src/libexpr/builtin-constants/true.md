---
name: 'true'
type: boolean
constructorArgs: ['true']
renameInGlobalScope: false
---
Primitive value.

It can be returned by
[comparison operators](@docroot@/language/operators.md#Comparison)
and used in
[conditional expressions](@docroot@/language/constructs.md#Conditionals).

The name `true` is not special, and can be shadowed:

```nix-repl
nix-repl> let true = 1; in true
1
```
