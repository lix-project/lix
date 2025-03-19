---
synopsis: "Fix macOS sandbox profile size errors"
issues: [fj#752, fj#718]
cls: [2861]
category: Fixes
credits: ["p-e-meunier", "poliorcetics"]
---

Fixed an issue on macOS where the sandbox profile could exceed size limits when building derivations with many dependencies. The profile is now split into multiple allowed sections to stay under the interpreter's limits.

This resolves errors like

```
error: (failed with exit code 1, previous messages: sandbox initialization failed: data object length 65730 exceeds maximum (65535)|failed to configure sandbox)

       error: unexpected EOF reading a line
```
