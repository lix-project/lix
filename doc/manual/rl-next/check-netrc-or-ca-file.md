---
synopsis: "check for missing ca-file or netrc-file if one is specified"
cls: [5646]
category: "Improvements"
credits: [astreaprtcl]
issues: [fj#1106]
---

If the settings `ssl-cert-file` or `netrc-file` have been set by the user, check if those files actually exist and fail if they are missing.
