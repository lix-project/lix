---
synopsis: "`builtins.storePath` is now allowed in pure contexts"
cls: [5285]
issues: [fj#402]
category: "Features"
credits: [raito, horrors]
---

`builtins.storePath` allows you to exploit your "external" knowledge about a store path and reuse it to avoid a (needless) copy to the store.

In Lix, pure evaluation ensures that you cannot depend on the state of things that were not locked down as part of your inputs to achieve the vision of "outputs are a pure function of inputs".

In this case, `builtins.storePath` allows you to depend on the state of your store and was therefore deemed an impure built-in.

Unfortunately, it is possible to replicate `builtins.storePath` functionality in the pure context with a clever application of `appendContext`:

```nix
{
  storePath = path:
  let path' = builtins.unsafeDiscardStringContext path;
  in
  # NOTE: merging the context set in all generality is impossible
because getContext on a pure path doesn't work.
    builtins.appendContext path' { ${path'} = { path = true; }; };
}
```

Out of pragmatism in the context of this possibility, `builtins.storePath` is now useable in pure contexts.
