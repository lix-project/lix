---
name: path
args: [args]
---
An enrichment of the built-in path type, based on the attributes
present in *args*. All are optional except `path`:

  - path\
    The underlying path.

  - name\
    The name of the path when added to the store. This can used to
    reference paths that have nix-illegal characters in their names,
    like `@`.

  - filter\
    A function of the type expected by `builtins.filterSource`,
    with the same semantics.

  - recursive\
    When `false`, when `path` is added to the store it is with a
    flat hash, rather than a hash of the NAR serialization of the
    file. Thus, `path` must refer to a regular file, not a
    directory. This allows similar behavior to `fetchurl`. Defaults
    to `true`.

  - sha256\
    When provided, this is the expected hash of the file at the
    path. Evaluation will fail if the hash is incorrect, and
    providing a hash allows `builtins.path` to be used even when the
    `pure-eval` nix config option is on.
