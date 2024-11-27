---
name: builtins
type: attrs
constructorArgs: [mem.buildBindings(symbols, 128).finish()]
renameInGlobalScope: false
---
Contains all the [built-in functions](@docroot@/language/builtins.md) and values.

Since built-in functions were added over time, [testing for attributes](./operators.md#has-attribute) in `builtins` can be used for graceful fallback on older Nix installations:

```nix
# if hasContext is not available, we assume `s` has a context
if builtins ? hasContext then builtins.hasContext s else true
```
