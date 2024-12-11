---
synopsis: "`nix repl` correctly tab-completes attribute names that require quotes"
cls: [1783]
credits: [ian-h-chamberlain]
category: Improvements
---

The REPL (`nix repl`) now includes quotes as part of attribute names while completing with `<TAB>`,
if necessary. For example, attribute names like `"hello@example.com"` or `"hello world"` would
be suggested without quotes, resulting in invalid syntax.
