---
name: shadow-internal-symbols
internalName: ShadowInternalSymbols
---
Allow shadowing the symbols `__sub`, `__mul`, `__div`, `__lessThan`, `__findFile` when used in the internal AST expansion. (`5 - 3` expands to `__sub 5 3` etc.)
