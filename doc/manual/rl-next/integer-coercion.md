---
synopsis: Experimental integer coercion in interpolated strings
issues: []
cls: [3198]
category: "Features"
credits: [raito, delroth, horrors, winter]
---

Ever tried interpolating a port number in Lix and ended up with something like this?

```nix
"http://${config.network.host}:${builtins.toString config.network.port}/"
```

You're not alone. Thousands of Lix users suffer every day from excessive `builtins.toString` syndrome. It’s 2025, and we still have to cast integers to use them in strings.

To address this, Lix introduces the **`coerce-integers`** experimental feature. When enabled, interpolated integers within `"${...}"` are automatically coerced to strings. This allows writing:

```nix
"http://${config.network.host}:${config.network.port}/"
```

without additional conversion.

To enable the feature, you need to add `coerce-integers` to your set of experimental features.

### Stabilization criteria

The `coerce-integers` feature is experimental and limited strictly to string interpolation (`"${...}"`). Before stabilization, the following must hold:

1. **Interpolation-only**
   Coercion must not occur outside interpolation. Expressions like `"" + 42` must continue to fail.

2. **Expectation that no explicit cast are being observed**
   Cases observing explicit coercion (e.g., via `tryEval` gadget or similar) are expected not to be load-bearing in actual production code.

### Timeline for stabilization

If the feature proves safe and is widely adopted across typical usage (e.g., actual configurations in the wild turning on the flag, non-trivial out-of-tree projects using it), the experimental flag will be removed **after six months of active use or two Lix releases**, whichever is longer.

This avoids locking the feature in experimental status indefinitely, as happened with Flakes, while allowing time for validation and ecosystem integration.

### What about coercing floats or more?

Coercion beyond integers -- such as for floats or other types -- is **not planned**, even under an experimental flag. Questions like "what is the canonical string representation of a float?" involve subtle and context-dependent trade-offs. Without a robust and principled mechanism to define and audit such behavior, introducing broader coercion risks setting unintended and hard-to-reverse precedents. The scope of `coerce-integers` is intentionally narrow and will remain so.

In terms of outlook, a proposal like https://git.lix.systems/lix-project/lix/issues/835 could pave the way for a better solution.
