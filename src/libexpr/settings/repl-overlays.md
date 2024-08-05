---
name: repl-overlays
internalName: replOverlays
settingType: PathsSetting<Paths>
default: []
---
A list of files containing Nix expressions that can be used to add
default bindings to [`nix
repl`](@docroot@/command-ref/new-cli/nix3-repl.md) sessions.

Each file is called with three arguments:
1. An [attribute set](@docroot@/language/values.html#attribute-set)
   containing at least a
   [`currentSystem`](@docroot@/language/builtin-constants.md#builtins-currentSystem)
   attribute (this is identical to
   [`builtins.currentSystem`](@docroot@/language/builtin-constants.md#builtins-currentSystem),
   except that it's available in
   [`pure-eval`](@docroot@/command-ref/conf-file.html#conf-pure-eval)
   mode).
2. The top-level bindings produced by the previous `repl-overlays`
   value (or the default top-level bindings).
3. The final top-level bindings produced by calling all
   `repl-overlays`.

For example, the following file would alias `pkgs` to
`legacyPackages.${info.currentSystem}` (if that attribute is defined):

```nix
info: final: prev:
if prev ? legacyPackages
   && prev.legacyPackages ? ${info.currentSystem}
then
{
  pkgs = prev.legacyPackages.${info.currentSystem};
}
else
{ }
```

Here's a more elaborate `repl-overlay`, which provides the following
variables:
- The original, unmodified variables are aliased to `original`.
- `legacyPackages.${system}` (if it exists) or `packages.${system}`
  (otherwise) is aliased to `pkgs`.
- All attribute set variables with a `${system}` attribute are
  abbreviated in the same manner; e.g. `devShells.${system}` is
  shortened to `devShells`.

For example, the following attribute set:

```nix
info: final: attrs: let
  # Equivalent to nixpkgs `lib.optionalAttrs`.
  optionalAttrs = predicate: attrs:
    if predicate
    then attrs
    else {};

  # If `attrs.${oldName}.${info.currentSystem}` exists, alias `${newName}` to
  # it.
  collapseRenamed = oldName: newName:
    optionalAttrs (builtins.hasAttr oldName attrs
      && builtins.hasAttr info.currentSystem attrs.${oldName})
    {
      ${newName} = attrs.${oldName}.${info.currentSystem};
    };

  # Alias `attrs.${oldName}.${info.currentSystem} to `${newName}`.
  collapse = name: collapseRenamed name name;

  # Alias all `attrs` keys with an `${info.currentSystem}` attribute.
  collapseAll =
    builtins.foldl'
    (prev: name: prev // collapse name)
    {}
    (builtins.attrNames attrs);
in
  # Preserve the original bindings as `original`.
  (optionalAttrs (! attrs ? original)
    {
      original = attrs;
    })
  // (collapseRenamed "packages" "pkgs")
  // (collapseRenamed "legacyPackages" "pkgs")
  // collapseAll
```
