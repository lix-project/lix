---
name: pure-eval
internalName: pureEval
type: bool
default: false
---
Pure evaluation mode ensures that the result of Nix expressions is fully determined by explicitly declared inputs, and not influenced by external state:

- File system and network access is restricted to accesses to immutable data only:
  - Path literals relative to the home directory like `~/lix` are rejected at parse time.
  - Access to absolute paths that did not result from Nix language evaluation is rejected when such paths are given as parameters to builtins like, for example, [`builtins.readFile`](@docroot@/language/builtins.md#builtins-readFile).

    Access is nonetheless allowed to (absolute) paths in the Nix store that are returned by builtins like [`builtins.filterSource`](@docroot@/language/builtins.md#builtins-filterSource), [`builtins.fetchTarball`](@docroot@/language/builtins.md#builtins-fetchTarball) and similar.
  - Impure fetches such as not specifying a commit ID for `builtins.fetchGit` or not specifying a hash for `builtins.fetchTarball` are rejected.
  - In flakes, access to relative paths outside of the root of the flake's source tree (often, a git repository) is rejected.
- The evaluator ignores `NIX_PATH`, `-I` and the `nix-path` setting. Thus, [`builtins.nixPath`](@docroot@/language/builtin-constants.md#builtins-nixPath) is an empty list.
- The builtins [`builtins.currentSystem`](@docroot@/language/builtin-constants.md#builtins-currentSystem) and [`builtins.currentTime`](@docroot@/language/builtin-constants.md#builtins-currentTime) are absent from `builtins`.
- [`builtins.getEnv`](@docroot@/language/builtin-constants.md#builtins-currentSystem) always returns empty string for any variable.
- [`builtins.storePath`](@docroot@/language/builtins.md#builtins-storePath) throws an error (Lix may change this, tracking issue: <https://git.lix.systems/lix-project/lix/issues/402>)
