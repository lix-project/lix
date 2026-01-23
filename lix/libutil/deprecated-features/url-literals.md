---
name: url-literals
internalName: UrlLiterals
timeline:
  - date: 2024-08-17
    release: 2.92.0
    cls: [1785]
    message: Removed the old `no-url-literals` experimental feature and turned it into a parse error by default.
---
*URL literals* are unquoted string literals containing URLs directly as part of the Nix language syntax.
This was deprecated because it needlessly complicates the syntax for the little benefit of merely saving two characters.
Additionally, it is a cause of inconsistencies in the language: `x:x` is a (URL) string but `x: x` and `_:x` are functions.

To fix this, put the URL in string quotation marks instead: `{ url = https://github.com/NixOS/nixpkgs; }` → `{ url = "https://github.com/NixOS/nixpkgs"; }`
