---
name: nix-path
internalName: nixPath
type: Strings
defaultExpr: 'getDefaultNixPath()'
defaultText: '*machine-specific*'
---
List of directories to be searched for `<...>` file references

In particular, outside of [pure evaluation mode](#conf-pure-eval), this determines the value of
[`builtins.nixPath`](@docroot@/language/builtin-constants.md#builtins-nixPath).
