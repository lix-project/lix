---
synopsis: Add hyperlinks in attr set printing
issues: []
cls: [3790]
category: Features
credits: [jade]
---

The attribute set printer, such as is seen in `nix repl` or in type errors, now prints hyperlinks on each attribute name to its definition site if it is known.

Example: all of the attributes shown here are hyperlinks to the exact definition site of the attribute in question:

```
$ nix eval -f '<nixpkgs>' lib.licenses.mit
{ deprecated = false; free = true; fullName = "MIT License"; redistributable = true; shortName = "mit"; spdxId = "MIT"; url = "https://spdx.org/licenses/MIT.html"; }
```
