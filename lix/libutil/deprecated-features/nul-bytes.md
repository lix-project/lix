---
name: nul-bytes
internalName: NulBytes
---
Allow NUL bytes (`\0`) in Nix strings.
Note however that due to Nix using NUL-terminated strings internally, this may cause undefined behavior.
