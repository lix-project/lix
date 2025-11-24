---
synopsis: "Warn instead of erroring when the final destination of a transfer changes in-flight"
cls: [4641]
issues: [fj#1004]
category: "Miscellany"
credits: [thubrecht]
---

Lix will now emit a warning during downloads where the final destination changes suddently mid-transfer instead of throwing an error.
This transfer behavior has been known to happen very rarely while fetching from some CDNs.
