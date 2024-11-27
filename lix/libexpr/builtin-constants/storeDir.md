---
name: storeDir
type: string
constructorArgs: [storeDir]
---
Logical file system location of the [Nix store](@docroot@/glossary.md#gloss-store) currently in use.

This value is determined by the `store` parameter in [Store URLs](@docroot@/command-ref/new-cli/nix3-help-stores.md):

```shell-session
$ nix-instantiate --store 'dummy://?store=/blah' --eval --expr builtins.storeDir
"/blah"
```
