---
synopsis: Remove the `parse-toml-timestamps` experimental feature
category: "Breaking Changes"
credits: [emilazy]
---

The `parse-toml-timestamps` experimental feature has been removed.

This feature used in‐band signalling to mark timestamps, making it
impossible to unambiguously parse TOML documents. It also exposed
implementation‐defined behaviour in the TOML specification that
changed in the toml11 parser library.

Any interface for parsing TOML timestamps suitable for future
stabilization would necessarily involve breaking changes, and there
is no evidence this experimental feature is being relied upon in the
wild, so it has been removed.
