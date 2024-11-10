---
name: builders-use-substitutes
internalName: buildersUseSubstitutes
type: bool
default: false
---
If set to `true`, Lix will instruct remote build machines to use
their own binary substitutes if available. In practical terms, this
means that remote hosts will fetch as many build dependencies as
possible from their own substitutes (e.g, from `cache.nixos.org`),
instead of waiting for this host to upload them all. This can
drastically reduce build times if the network connection between
this computer and the remote build host is slow.
