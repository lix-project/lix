---
synopsis: Reject overflowing TOML integer literals
issues: []
cls: [3916]
category: "Breaking Changes"
credits: [emilazy]
---

The toml11 library used by Lix was updated. The new
version aligns with the [TOML v1.0.0 specification’s
requirement](https://toml.io/en/v1.0.0#integer) to reject integer
literals that cannot be losslessly parsed. This means that code like
`builtins.fromTOML "v=0x8000000000000000"` will now produce an error
rather than silently saturating the integer result.
