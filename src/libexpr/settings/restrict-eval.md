---
name: restrict-eval
internalName: restrictEval
type: bool
default: false
---
If set to `true`, the Nix evaluator will not allow access to any
files outside of the Nix search path (as set via the `NIX_PATH`
environment variable or the `-I` option), or to URIs outside of
[`allowed-uris`](../command-ref/conf-file.md#conf-allowed-uris).
The default is `false`.
