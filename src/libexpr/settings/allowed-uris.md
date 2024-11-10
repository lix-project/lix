---
name: allowed-uris
internalName: allowedUris
type: Strings
default: []
---
A list of URI prefixes to which access is allowed in restricted
evaluation mode. For example, when set to
`https://github.com/NixOS`, builtin functions such as `fetchGit` are
allowed to access `https://github.com/NixOS/patchelf.git`.
