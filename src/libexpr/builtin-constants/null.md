---
name: 'null'
type: 'null'
constructorArgs: []
renameInGlobalScope: false
---
Primitive value.

The name `null` is not special, and can be shadowed:

```nix-repl
nix-repl> let null = 1; in null
1
```
