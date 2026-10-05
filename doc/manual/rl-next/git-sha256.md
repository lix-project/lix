---
synopsis: "Git fetchers supports SHA-256 repositories"
issues: [fj#1294, fj#1293, fj#619]
cls: [6518]
category: Features
credits: [raito]
---

`builtins.fetchGit`, `fetchTree`, and flake inputs backed by Git now work with
repositories that use the SHA-256 object format, independently of the client's
`init.defaultObjectFormat` / `GIT_DEFAULT_HASH`.

This also fixes fetching SHA-1 repositories on machines whose git is configured
to create SHA-256 repositories by default, which would previously fail with
`mismatched algorithms: client sha256; server sha1`.

Fetching a SHA-256 repository requires git 2.29 or newer. Older versions now
report a clear error instead of an object ID length mismatch.
