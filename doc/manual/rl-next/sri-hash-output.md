---
synopsis: "Consistently use SRI hashes in hash mismatch errors"
cls: [2868]
category: Improvements
credits: jade
---
Previously there were a few weird cases (flake inputs, e.g., among others) where Lix would print the old Nix base-32 hash format (sha256:abcd...) rather than the newer [SRI base64 format](https://developer.mozilla.org/en-US/docs/Web/Security/Subresource_Integrity) (sha256-AAAA...) that is used in most Lix hash mismatch errors.
This made it annoying to compare them to hashes shown by most of the modern UI surface of Lix which uses SRI.
