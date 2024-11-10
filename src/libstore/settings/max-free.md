---
name: max-free
internalName: maxFree
type: uint64_t
# n.b. this is deliberately int64 max rather than uint64 max because
# this goes through the Nix language JSON parser and thus needs to be
# representable in Nix language integers.
defaultExpr: 'std::numeric_limits<int64_t>::max()'
defaultText: '*infinity*'
---
When a garbage collection is triggered by the `min-free` option, it
stops as soon as `max-free` bytes are available. The default is
infinity (i.e. delete all garbage).
