---
synopsis: "Emit warnings when encountering IFD with `warn-import-from-derivation`"
prs: [nix#13279]
cls: [3879]
category: Features
credits: [getchoo, gustavderdrache, edolstra]
---

Instead of only being able to toggle the use of [Import from
Derivation](https://nix.dev/manual/nix/stable/language/import-from-derivation) with
`allow-import-from-derivation`, Lix is now able to warn users whenever IFD is encountered with
`warn-import-from-derivation`.
