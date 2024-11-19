---
name: require-sigs
internalName: requireSigs
type: bool
default: true
---
If set to `true` (the default), any non-content-addressed path added
or copied to the Nix store (e.g. when substituting from a binary
cache) must have a signature by a trusted key. A trusted key is one
listed in `trusted-public-keys`, or a public key counterpart to a
private key stored in a file listed in `secret-key-files`.

Set to `false` to disable signature checking and trust all
non-content-addressed paths unconditionally.

(Content-addressed paths are inherently trustworthy and thus
unaffected by this configuration option.)
