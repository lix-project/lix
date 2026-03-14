---
synopsis: "libexpr: allow empty attr-names in parseAttrPath if they are quoted"
cls: [5375]
category: "Miscellany"
credits: [ma27]
---

Empty strings are now allowed in attribute paths as consumed by e.g. `nix-build`.
I.e. `nix-build -A 'foo."".bar'` works now.
The quotes are necessary, i.e. `nix-build -A foo..bar` will throw an error.
