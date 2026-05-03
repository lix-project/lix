---
name: rpc-sockets
internalName: RpcSockets
---

Enable the experimental RPC sockets. This makes the `lix-xp-1` daemon protocol available for clients.
Note that this feature only enables the *daemon* side of this features; clients always support all of
the protocols (although by default only the legacy sockets are tried without explicit configuration).
