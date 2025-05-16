---
synopsis: Better debuggability on fixed-output hash mismatches
issues: []
cls: []
category: Improvements
credits: [lheckemann]
---

Fixed-output derivation hash mismatch error messages will now include the path that was
produced unexpectedly, and this path will be registered as valid even if `--check`
(`nix-store`, `nix-build`) or `--rebuild` (`nix build`) was passed. This makes comparing
the expected path with the obtained path easier, and is useful for debugging when
upstreams modify previously-published releases or when changes in fixed-output
derivations' dependencies affect their output unexpectedly.
