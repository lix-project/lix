# Name

`nix-store --delete` - delete store paths

# Synopsis

`nix-store` `--delete` [`--ignore-liveness`] [`--skip-live`] [`--delete-closure`] *paths…*

# Description

The operation `--delete` deletes the store paths *paths* from the Nix
store, but only if it is safe to do so; that is, when the path is not
reachable from a root of the garbage collector. This means that you can
only delete paths that would also be deleted by `nix-store --gc`. Thus,
`--delete` is a more targeted version of `--gc`.

With the option `--ignore-liveness`, reachability from the roots is
ignored. However, the path still won’t be deleted if there are other
paths in the store that refer to it (i.e., depend on it).

This operation will raise an error if any of the paths are still live
and `--ignore-liveness` is not passed. Passing `--skip-live` will
prevent this from being considered an error.

The option `--delete-closure` will also attempt to delete any paths
that are in the given path's dependency closure.

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ../env-common.md}}

# Example

```console
$ nix-store --delete /nix/store/zq0h41l75vlb4z45kzgjjmsjxvcv1qk7-mesa-6.4
0 bytes freed (0.00 MiB)
error: cannot delete path `/nix/store/zq0h41l75vlb4z45kzgjjmsjxvcv1qk7-mesa-6.4' since it is still alive
```
