---
name: fetchTree
args: [spec]
experimentalFeature: flakes
renameInGlobalScope: false
---
Fetches the tree specified by the attribute set or URL `spec`.

The spec is in the form of [a flake reference](../command-ref/new-cli/nix3-flake.md#flake-references); flake references are a thin wrapper around `fetchTree`.

There are [some efforts](https://github.com/nix-community/fetchTree-spec) to document the behaviour of `fetchTree` independently of flakes, but they have not yet borne fruit as of 2025-03.

`spec` also accepts the following special attribute not documented there:
  - name\
    The name of the resulting store path to fetch to.
    Optional; defaults to the basename of the URL.

    Due to some vagaries of flake behaviour, naming the fetched input `source` may avoid some extra copying when using the resulting store path as a path input for a flake.
    See <https://git.lix.systems/lix-project/lix/issues/630>.
