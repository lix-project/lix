---
name: scopedImport
implementation: "[](EvalState & state, Value ** args, Value & v) { import(state, *args[1], args[0], v); }"
args: [scope, path]
renameInGlobalScope: false
---

> **Warning**
>
> This builtin's use is heavily discouraged, it has many drawbacks and may be removed
> in a future version of Lix.

Functions like [`import`](#builtins-import) with the exception that
it takes a `scope`, which is a set of attributes to be added to the
lexical scope of the expression.

This essentially allows overriding the ambient builtin variables.

For example, if `foo.nix` is a file containing the following content:

```nix
x
```

then the following expression

```nix
scopedImport { x = 1; } ./foo.nix
```

will evaluate to `1`.

Another application is overriding builtin functions or constants, e.g. to
trace all calls to `map`, one can do:

```nix
let
  overrides = {
    map = f: xs: builtins.trace "map called!" (map f xs);

    # Ensure that our override gets propagated by calls to
    # import/scopedImport.
    import = fn: scopedImport overrides fn;

    scopedImport = attrs: fn: scopedImport (overrides // attrs) fn;

    # Also update ‘builtins’.
    builtins = builtins // overrides;
  };
in scopedImport overrides ./bla.nix
```

Similarly, it can be used to extend the set of builtin functions.

> **Warning**
>
> One of the downsides of `scopedImport` is that it bypasses the evaluation cache.
> This means that importing a file multiple times will lead to multiple expensive
> parsings and evaluations.
