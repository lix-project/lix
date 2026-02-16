---
synopsis: "nix store add-path now supports references"
cls: [5205]
category: "Features"
credits: [jade]
---
Lix supports two categories of hashes in store paths: input-addressed and output-addressed.

Currently, in Nix language, there is no way to produce output-addressed paths with references, as fixed-output derivations forbid references.
However, the Nix store actually *supports* references in output-addressed paths.
This is very useful for importing build products created outside of Lix that reference dependency store paths since such build products have no associated derivation so don't make any sense to input-address.
Previously, output-addressed paths with references could only be created by writing a custom client to the rather-baroque Nix daemon protocol; now it's available in the CLI.

Using `nix store add-path --references-list-json REFS_LIST_FILE SOME_PATH` with a JSON list of string store paths, you can now create such paths with the Lix CLI.
They may be consumed from Nix language using something like `builtins.storePath` or the following which also works in pure evaluation mode:

```nix
# Hack from https://git.lix.systems/lix-project/lix/issues/402#issuecomment-5889
path:
builtins.appendContext path {
  ${path} = {
    path = true;
  };
}
```
