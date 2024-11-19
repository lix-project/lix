---
name: keep-outputs
internalName: gcKeepOutputs
type: bool
default: false
aliases: [gc-keep-outputs]
---
If `true`, the garbage collector will keep the outputs of
non-garbage derivations. If `false` (default), outputs will be
deleted unless they are GC roots themselves (or reachable from other
roots).

In general, outputs must be registered as roots separately. However,
even if the output of a derivation is registered as a root, the
collector will still delete store paths that are used only at build
time (e.g., the C compiler, or source tarballs downloaded from the
network). To prevent it from doing so, set this option to `true`.
