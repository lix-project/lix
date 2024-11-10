---
name: secret-key-files
internalName: secretKeyFiles
type: Strings
default: []
---
A whitespace-separated list of files containing secret (private)
keys. These are used to sign locally-built paths. They can be
generated using `nix-store --generate-binary-cache-key`. The
corresponding public key can be distributed to other users, who
can add it to `trusted-public-keys` in their `nix.conf`.
